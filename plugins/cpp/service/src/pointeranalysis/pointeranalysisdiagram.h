#ifndef CC_SERVICE_LANGUAGE_POINTERANALYSISDIAGRAM_H
#define CC_SERVICE_LANGUAGE_POINTERANALYSISDIAGRAM_H

#include <service/cppservice.h>
#include <projectservice/projectservice.h>
#include <util/graph.h>

#include <model/cpppointeranalysis.h>

namespace cc
{
namespace service
{
namespace language
{

class PointerAnalysisDiagram
{
public:
  PointerAnalysisDiagram(
    std::shared_ptr<odb::database> db_,
    std::shared_ptr<std::string> datadir_,
    const boost::program_options::variables_map& config_);

  /**
   * Generate pointer analysis diagram for Andersen style points-to analysis.
   */
  void getAndersenPointerAnalysisDiagram(
    util::Graph& graph_,
    const core::AstNodeId& astNodeId_);

  /**
   * Generate pointer analysis diagram for Steensgaard style points-to analysis.
   */
  void getSteensgaardPointerAnalysisDiagram(
    util::Graph& graph_,
    const core::AstNodeId& astNodeId_);

private:
  typedef std::vector<std::pair<std::string, std::string>> Decoration;

  void createMemorySubraphs(util::Graph& graph_);

  bool getAstNode(
    std::uint64_t mangledNameHash_,
    std::unordered_map<std::uint64_t, model::CppAstNode>& astNodeCache_);

  void cratePointerAnalysisNode(
    util::Graph& graph_,
    const std::string& nodeId_,
    const std::set<model::CppPointerAnalysis::Options>& options_,
    bool addToSubraphs = false);

  void decoratePointerAnalysisNode(
    util::Graph& graph_,
    const model::CppAstNode& astNode,
    const std::set<model::CppPointerAnalysis::Options>& options_);

  /**
   * This function decorates a graph node.
   * @param graph_ A graph object.
   * @param elem_ A graph node
   * @param decoration_ A map which describes the style attributes.
   */
  void decorateNode(
    util::Graph& graph_,
    const util::Graph::Node& node_,
    const Decoration& decoration_) const;

  /**
   * This function decorates a graph subgraph.
   * @param graph_ A graph object.
   * @param elem_ A graph subgraph
   * @param decoration_ A map which describes the style attributes.
   */
  void decorateSubgraph(
    util::Graph& graph_,
    const util::Graph::Subgraph& subgrap_,
    const Decoration& decoration_) const;

  static const Decoration centerNodeDecoration;
  static const Decoration stackNodeDecoration;
  static const Decoration objectNodeDecoration;
  static const Decoration undefinedNodeDecoration;
  static const Decoration nullptrNodeDecoration;
  static const Decoration functionNodeDecoration;

  static const Decoration globalMemorySubgraphDecoration;
  static const Decoration stackMemorySubgraphDecoration;
  static const Decoration heapMemorySubgraphDecoration;

  std::shared_ptr<odb::database> _db;
  util::OdbTransaction _transaction;
  const boost::program_options::variables_map& _config;

  CppServiceHandler _cppHandler;
  core::ProjectServiceHandler _projectHandler;
};

} // language
} // service
} // cc

#endif // CC_SERVICE_LANGUAGE_POINTERANALYSISDIAGRAM_H
