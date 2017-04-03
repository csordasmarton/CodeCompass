#ifndef CC_SERVICE_POINTERANALYSISSTMTCOLLECTOR_H_
#define CC_SERVICE_POINTERANALYSISSTMTCOLLECTOR_H_

#include <vector>

#include <model/cpppointeranalysis.h>
#include <model/cpppointeranalysis-odb.hxx>

#include <projectservice/projectservice.h>

namespace cc
{
namespace service
{

/**
 * Pointer analysis statement collector.
 */
class PointerAnalysisStmtCollector
{
public:
  PointerAnalysisStmtCollector(
    std::shared_ptr<odb::database> db_,
    const boost::program_options::variables_map& config_);

  /**
   * This function collects pointer analysis statements recursively from
   * database related to the `start_` parameter.
   * @param start_ Starting AST node mangled name hash where statements
   * collecting starts.
   * @return List of collected statements.
   */
  std::vector<model::CppPointerAnalysis> collect(const std::uint64_t& start_);

private:
  typedef typename odb::query<model::CppPointerAnalysis> PointerAnalysisQuery;
  typedef typename odb::result<model::CppPointerAnalysis> PointerAnalysisResult;

  /**
   * Note: It uses a neo4j plugin called APOC for better performance. It has to
   * be located in the neo4j plugins folder:
   * https://neo4j-contrib.github.io/neo4j-apoc-procedures/
   */
  std::vector<model::CppPointerAnalysis> collectFromNeo4jDb(
    const std::uint64_t& start_,
    const std::string& connStr_);

  std::shared_ptr<odb::database> _db;
  util::OdbTransaction _transaction;
  const boost::program_options::variables_map& _config;
};

} // service
} // cc

#endif // CC_SERVICE_POINTERANALYSISSTMTCOLLECTOR_H_
