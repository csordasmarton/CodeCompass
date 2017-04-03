#include <deque>
#include <errno.h>
#include <chrono>

#include <util/logutil.h>
#include <neo4j-client.h>

#include "pointeranalysisstmtcollector.h"

namespace
{

std::string toString(const neo4j_value_t& value_)
{
  char key[128];
  return neo4j_string_value(value_, key, sizeof(key));
}

 void setStmtSide(
  cc::model::CppPointerAnalysis::StmtSide& stmtSide_,
  const neo4j_value_t& properties_,
  bool isLhsSide = true)
{
  //--- Set mangled name hash ---//

  const neo4j_value_t lhsHash = neo4j_map_kget(
    properties_, neo4j_string(isLhsSide ? "lhs_hash" : "rhs_hash"));
  stmtSide_.mangledNameHash = std::stoull(toString(lhsHash));

  //--- Set operators ---//

  const neo4j_value_t lhsOperators = neo4j_map_kget(
    properties_, neo4j_string(isLhsSide ? "lhs_operators" : "rhs_operators"));
  stmtSide_.operators = toString(lhsOperators);

  //--- Set options ---//

  const neo4j_value_t lhsOptions = neo4j_map_kget(
    properties_, neo4j_string(isLhsSide ? "lhs_options" : "rhs_options"));

  if (neo4j_instanceof(lhsOptions, NEO4J_LIST))
    for (std::size_t i = 0; i < neo4j_list_length(lhsOptions); ++i)
    {
      const neo4j_value_t opt = neo4j_list_get(lhsOptions, i);
      stmtSide_.options.insert(static_cast<
        cc::model::CppPointerAnalysis::Options>(neo4j_int_value(opt)));
    }
}

/**
 * This function return true if a statement has to be skipped.
 */
bool skipStmt(
  const cc::model::CppPointerAnalysis& stmt_,
  const cc::model::CppPointerAnalysis::StmtSide& current_)
{
  return current_.mangledNameHash == stmt_.rhs.mangledNameHash &&
    (current_.options.find(cc::model::CppPointerAnalysis::Options::Return) !=
      current_.options.end() ||
    current_.options.find(cc::model::CppPointerAnalysis::Options::FunctionCall) !=
      current_.options.end());
}

}

namespace cc
{
namespace service
{

PointerAnalysisStmtCollector::PointerAnalysisStmtCollector(
  std::shared_ptr<odb::database> db_,
  const boost::program_options::variables_map& config_)
  : _db(db_), _transaction(db_), _config(config_)
{
}

std::vector<model::CppPointerAnalysis>
PointerAnalysisStmtCollector::collectFromNeo4jDb(
  const std::uint64_t& start_,
  const std::string& connStr_)
{
  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

  std::vector<model::CppPointerAnalysis> statements;

  neo4j_connection_t *connection = neo4j_connect(
     connStr_.c_str(), NULL, NEO4J_INSECURE);

  if (!connection)
  {
    LOG(error) << "Neo4j connection (" << connStr_ << ") failed.";
    return statements;
  }

  neo4j_session_t *session = neo4j_new_session(connection);

  if (session == NULL)
  {
    LOG(warning) << "Failed to start Neo4j session!";
    neo4j_perror(stderr, errno, "Failed to start session");
    return statements;
  }

//  std::string q =
//    "MATCH path = (a:StmtSide {hash : \"" + std::to_string(start_) + "\"})-[r:ASSIGN*..5]-(b:StmtSide) "
//    "WHERE NOT (b)-->() "
//    "WITH REDUCE(output = [], r IN collect(r) | output + REDUCE(innerOutput = [], innerR in r | innerOutput + innerR)) AS edges "
//    "unwind edges as statements "
//    "with distinct statements "
//    "unwind statements as u "
//    "return u";

  std::string q =
    "MATCH (a:StmtSide { hash:\"" + std::to_string(start_) + "\" }) "
    "CALL apoc.path.subgraphAll(a, {relationshipFilter:'ASSIGN', bfs: true, uniqueness:\"NODE_GLOBAL\"}) yield nodes, relationships "
    "unwind relationships as statements "
    "return statements";

  LOG(debug) << q;

  neo4j_result_stream_t *results = neo4j_run(session, q.c_str(), neo4j_null);

  if (!results)
  {
    LOG(warning) << "Failed to run statement!";
    return statements;
  }

  while (neo4j_result_t *result = neo4j_fetch_next(results))
  {
    if (results == NULL)
    {
      LOG(warning) << "Failed to fetch result!";
      return statements;
    }

    neo4j_value_t value = neo4j_result_field(result, 0);
    if (neo4j_instanceof(value, NEO4J_RELATIONSHIP))
    {
      neo4j_value_t properties = neo4j_relationship_properties(value);
      if (neo4j_instanceof(properties, NEO4J_MAP))
      {
        model::CppPointerAnalysis stmt;

        neo4j_value_t properties = neo4j_relationship_properties(value);
        const neo4j_value_t id =
          neo4j_map_kget(properties, neo4j_string("id"));
        stmt.id = std::stoull(toString(id));

        setStmtSide(stmt.lhs, properties);
        setStmtSide(stmt.rhs, properties, false);

        statements.push_back(std::move(stmt));
      }
    }
  }
  neo4j_close_results(results);

  neo4j_end_session(session);
  neo4j_close(connection);
  neo4j_client_cleanup();

  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

  LOG(debug)
      << "Pointer analysis collector with neo4j has been finished:\n"
      << "\t- collected statements: " << statements.size() << "\n"
      << "\t- time(ms): " << std::chrono::duration_cast<
         std::chrono::milliseconds>(end - begin).count();

  return statements;
}

std::vector<model::CppPointerAnalysis> PointerAnalysisStmtCollector::collect(
  const std::uint64_t& start_)
{
  std::vector<model::CppPointerAnalysis> neo4jStatements;
  if (_config.count("neo4j"))
  {
    neo4jStatements = collectFromNeo4jDb(start_, _config["neo4j"].as<std::string>());
  }

  LOG(debug) << "Start collecting pointer analysis statements.";

  std::chrono::steady_clock::time_point begin =
    std::chrono::steady_clock::now();

  std::vector<model::CppPointerAnalysis> statements;
  std::unordered_set<std::uint64_t> skipVariable;

  std::deque<model::CppPointerAnalysis::StmtSide> q;
  q.push_back({start_, "", {}});

  while (!q.empty())
  {
    model::CppPointerAnalysis::StmtSide current = q.front();

    _transaction([&, this](){
      PointerAnalysisResult stmts = _db->query<model::CppPointerAnalysis>(
        (PointerAnalysisQuery::lhs.mangledNameHash == current.mangledNameHash ||
        PointerAnalysisQuery::rhs.mangledNameHash == current.mangledNameHash));

      std::unordered_set<std::uint64_t> funcParams;
      for (model::CppPointerAnalysis& stmt : stmts)
      {
//        if (skipStmt(stmt, current))
//          continue;

//        if (stmt.lhs.options.find(model::CppPointerAnalysis::Options::Param) !=
//          stmt.lhs.options.end() && start_ != stmt.lhs.mangledNameHash)
//        {
//          if (!skipVariable.count(stmt.lhs.mangledNameHash))
//            funcParams.insert(stmt.lhs.mangledNameHash);
//          else
//            continue;
//        }

        if (std::find(statements.begin(), statements.end(), stmt) ==
            statements.end())
        {
          if (current.mangledNameHash == stmt.rhs.mangledNameHash)
            q.push_back(stmt.lhs);
          else
            q.push_back(stmt.rhs);

          statements.push_back(stmt);
        }
      }
      skipVariable.insert(funcParams.begin(), funcParams.end());
    });
    q.pop_front();
  }

  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

  LOG(debug)
    << "Pointer analysis collector has been finished:\n"
    << "\t- collected statements: " << statements.size() << "\n"
    << "\t- time(ms): " << std::chrono::duration_cast<
       std::chrono::milliseconds>(end - begin).count();

  return statements;
}

} // service
} // cc
