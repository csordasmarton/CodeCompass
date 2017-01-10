#include <deque>

#include <util/logutil.h>

#include "pointeranalysisstmtcollector.h"

namespace
{

/**
 * This function return true if a statement has to be skipped.
 */
bool skipStmt(
  const cc::model::CppPointerAnalysis& stmt_,
  const cc::model::CppPointerAnalysis::StmtSide& current_)
{
  return current_.mangledNameHash == stmt_.rhs.mangledNameHash &&
    current_.options.find(
      cc::model::CppPointerAnalysis::Options::Return) !=
    current_.options.end();
}


}

namespace cc
{
namespace service
{

PointerAnalysisStmtCollector::PointerAnalysisStmtCollector(
  std::shared_ptr<odb::database> db_) : _db(db_), _transaction(db_)
{
}

std::vector<model::CppPointerAnalysis> PointerAnalysisStmtCollector::collect(
  const std::uint64_t& start_)
{
  LOG(debug) << "Pointer analysis collector start.";
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
        if (skipStmt(stmt, current))
          continue;

        if (stmt.lhs.options.find(model::CppPointerAnalysis::Options::Param) !=
          stmt.lhs.options.end() && start_ != stmt.lhs.mangledNameHash)
        {
          if (!skipVariable.count(stmt.lhs.mangledNameHash))
            funcParams.insert(stmt.lhs.mangledNameHash);
          else
            continue;
        }

        if (std::find(statements.begin(), statements.end(), stmt) ==
            statements.end())
        {
          if (current.mangledNameHash == stmt.rhs.mangledNameHash)
            q.push_back(stmt.lhs);
          else
            q.push_back(stmt.rhs);

          if(stmt.rhs.mangledNameHash == current.mangledNameHash ||
             stmt.lhs.operators.find("*") != std::string::npos ||
             stmt.rhs.operators.find("*") != std::string::npos)
            statements.push_back(stmt);
          else
            statements.insert(statements.begin(), stmt);

          LOG(debug)
            << stmt.lhs.operators << stmt.lhs.mangledNameHash << " = "
            << stmt.rhs.operators << stmt.rhs.mangledNameHash;
        }
      }
      skipVariable.insert(funcParams.begin(), funcParams.end());
    });
    q.pop_front();
  }

  LOG(debug) << "Pointer analysis collector end.";

  return statements;
}

} // service
} // cc
