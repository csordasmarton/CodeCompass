#include <util/legendbuilder.h>
#include <util/util.h>

#include "pointeranalysisstmtcollector.h"
#include "andersen.h"
#include "steensgaard.h"
#include "pointeranalysisdiagram.h"

namespace
{

typedef odb::query<cc::model::CppAstNode> AstQuery;
typedef odb::result<cc::model::CppAstNode> AstResult;

}

namespace cc
{
namespace service
{
namespace language
{

PointerAnalysisDiagram::PointerAnalysisDiagram(
  std::shared_ptr<odb::database> db_,
  std::shared_ptr<std::string> datadir_,
  const boost::program_options::variables_map& config_)
    : _db(db_),
      _transaction(db_),
      _config(config_),
      _cppHandler(db_, datadir_, config_),
      _projectHandler(db_, datadir_, config_)
{
}

bool PointerAnalysisDiagram::getAstNode(
  std::uint64_t mangledNameHash_,
  std::unordered_map<std::uint64_t, model::CppAstNode>& astNodeCache_)
{
  if (!astNodeCache_.count(mangledNameHash_))
  {
    AstResult def = _db->query<model::CppAstNode>(
      AstQuery::mangledNameHash == mangledNameHash_ &&
      ( AstQuery::astType == model::CppAstNode::AstType::Declaration ||
        AstQuery::astType == model::CppAstNode::AstType::Definition  ||
        AstQuery::astType == model::CppAstNode::AstType::Other));

    if (def.empty())
      return false;

    astNodeCache_.insert(std::make_pair(mangledNameHash_, *def.begin()));
  }

  return true;
}

void PointerAnalysisDiagram::getAndersenPointerAnalysisDiagram(
  util::Graph& graph_,
  const core::AstNodeId& astNodeId_)
{
  graph_.setAttribute("rankdir", "LR");

  std::vector<AstNodeInfo> nodes;
  _cppHandler.getReferences(
    nodes, astNodeId_, CppServiceHandler::DEFINITION, {});

  if (nodes.empty())
    return;

  AstNodeInfo node = nodes.front();

  //--- Create subraphs for memory models: global, stack, heap ---//

  createMemorySubraphs(graph_);

  //--- Collect statements ---//

  PointerAnalysisStmtCollector collector(_db, _config);
  std::vector<model::CppPointerAnalysis> statements =
    collector.collect(node.mangledNameHash);

  //--- Run Andersen style points-to algorithm on the collected statements ---//

  Andersen algorithm;
  Andersen::PointsToSet pointsToSet = algorithm.run(statements);

  //--- Draw pointer analysis diagram ---//

  std::unordered_map<std::uint64_t, model::CppAstNode> astNodeCache;
  _transaction([&, this]{
    for (const auto& key : pointsToSet)
    {
      model::CppPointerAnalysis::StmtSide lhsSide = key.first;

      if (!getAstNode(lhsSide.mangledNameHash, astNodeCache))
      {
        LOG(warning)
          << "No CppAstNode for lhs side: " << lhsSide.mangledNameHash;
        continue;
      }

      LOG(debug) << "LHS: " << lhsSide.mangledNameHash;

      model::CppAstNode& lhsNode = astNodeCache.at(lhsSide.mangledNameHash);
      std::string lhsNodeId = std::to_string(lhsNode.id);
      if (!graph_.hasNode(lhsNodeId))
      {
        cratePointerAnalysisNode(graph_, lhsNodeId, key.first.options, true);
        decoratePointerAnalysisNode(graph_, lhsNode, key.first.options);
      }

      for (const auto& value : key.second)
      {
        LOG(debug)
          << "RHS: " << value.operators << "ID: " << value.mangledNameHash;

        if (!getAstNode(value.mangledNameHash, astNodeCache))
        {
          LOG(warning)
            << "No CppAstNode for rhs side: " << value.mangledNameHash;
          continue;
        }

        model::CppAstNode& rhsNode = astNodeCache.at(value.mangledNameHash);

        std::string rhsNodeId = std::to_string(rhsNode.id);

        if (!graph_.hasNode(rhsNodeId))
        {
          cratePointerAnalysisNode(graph_, rhsNodeId, value.options, true);
          decoratePointerAnalysisNode(graph_, rhsNode, value.options);
        }

        util::Graph::Edge edge = graph_.createEdge(
          lhsNodeId, rhsNodeId);
        graph_.setEdgeAttribute(edge, "id", lhsNodeId + "_" + rhsNodeId);
        if (model::isReference(lhsSide))
          graph_.setEdgeAttribute(edge, "style", "dashed");
      }
    }
  });

  if (graph_.hasNode(node.id))
    decorateNode(graph_, node.id, centerNodeDecoration);
}

void PointerAnalysisDiagram::getSteensgaardPointerAnalysisDiagram(
  util::Graph& graph_,
  const core::AstNodeId& astNodeId_)
{
  graph_.setAttribute("rankdir", "LR");

  std::vector<AstNodeInfo> nodes;
  _cppHandler.getReferences(
    nodes, astNodeId_, CppServiceHandler::DEFINITION, {});

  if (nodes.empty())
    return;

  AstNodeInfo node = nodes.front();

  //--- Create subraphs for memory models: global, stack, heap ---//

  createMemorySubraphs(graph_);

  //--- Collect statements ---//

  PointerAnalysisStmtCollector collector(_db, _config);
  std::vector<model::CppPointerAnalysis> statements =
    collector.collect(node.mangledNameHash);

  //--- Run Steensgaard style points-to algorithm on the collected statements ---//

  Steensgaard algorithm;
  std::map<model::CppPointerAnalysis::StmtSide, Steensgaard::TypeNodePtr> res =
    algorithm.run(statements);

  //--- Draw pointer analysis diagram ---//

  std::unordered_map<std::uint64_t, model::CppAstNode> astNodeCache;
  _transaction([&, this]{
    for (const auto& key : res)
    {
      model::CppPointerAnalysis::StmtSide lhsSide = key.first;

      if (!getAstNode(lhsSide.mangledNameHash, astNodeCache))
      {
        LOG(warning)
          << "No CppAstNode for lhs side: " << lhsSide.mangledNameHash;
        continue;
      }

      LOG(debug) << "LHS: " << lhsSide.mangledNameHash;

      model::CppAstNode& lhsNode = astNodeCache.at(lhsSide.mangledNameHash);
      std::string lhsNodeId = std::to_string(lhsNode.id);

      if (!graph_.hasNode(lhsNodeId))
      {
        std::string subgraphId =
          std::to_string(key.second->value.mangledNameHash);
        util::Graph::Subgraph subgraph =
          graph_.getOrCreateSubgraph(subgraphId);
        graph_.getOrCreateNode(lhsNodeId, subgraph);

        decoratePointerAnalysisNode(graph_, lhsNode, key.first.options);
      }

      if (!key.second->pointsTo)
         continue;

      model::CppPointerAnalysis::StmtSide rhsSide = key.second->pointsTo->value;

      LOG(debug)
        << "RHS: " << rhsSide.operators << "ID: " << rhsSide.mangledNameHash;

      if (!getAstNode(rhsSide.mangledNameHash, astNodeCache))
      {
        LOG(warning)
          << "No CppAstNode for rhs side: " << rhsSide.mangledNameHash;
        continue;
      }

      model::CppAstNode& rhsNode = astNodeCache.at(rhsSide.mangledNameHash);
      std::string rhsNodeId = std::to_string(rhsNode.id);

      if (!graph_.hasNode(rhsNodeId))
      {
        std::string subgraphId = std::to_string(rhsSide.mangledNameHash);
        util::Graph::Subgraph subgraph =
          graph_.getOrCreateSubgraph(subgraphId);
        graph_.getOrCreateNode(rhsNodeId, subgraph);

        decoratePointerAnalysisNode(graph_, rhsNode, rhsSide.options);
      }

      util::Graph::Edge edge = graph_.createEdge(
        lhsNodeId, rhsNodeId);
      graph_.setEdgeAttribute(edge, "id", lhsNodeId + "_" + rhsNodeId);
      if (model::isReference(lhsSide))
        graph_.setEdgeAttribute(edge, "style", "dashed");
    }
  });

  if (graph_.hasNode(node.id))
    decorateNode(graph_, node.id, centerNodeDecoration);
}

void PointerAnalysisDiagram::cratePointerAnalysisNode(
  util::Graph& graph_,
  const std::string& nodeId_,
  const std::set<model::CppPointerAnalysis::Options>& options_,
  bool addToSubraphs)
{
  if (!graph_.hasNode(nodeId_))
  {
    bool created = false;
    if (addToSubraphs)
      for (const auto& option : options_)
      {
        switch (option)
        {
          case model::CppPointerAnalysis::Options::HeapObj:
            graph_.getOrCreateNode(nodeId_, "cluster_heap");
            created = true;
            break;

          case model::CppPointerAnalysis::Options::StackObj:
            graph_.getOrCreateNode(nodeId_, "cluster_stack");
            created = true;
            break;

          case model::CppPointerAnalysis::Options::GlobalObject:
            graph_.getOrCreateNode(nodeId_, "cluster_global");
            created = true;
            break;
        }

      if (created)
        break;
    }

    if (!created)
      graph_.getOrCreateNode(nodeId_);
  }
}

void PointerAnalysisDiagram::decoratePointerAnalysisNode(
  util::Graph& graph_,
  const model::CppAstNode& astNode,
  const std::set<model::CppPointerAnalysis::Options>& options_)
{
  std::string nodeId = std::to_string(astNode.id);
  graph_.setNodeAttribute(nodeId, "label", astNode.astValue);

  for (const auto& option : options_)
  {
    switch (option)
    {
      case model::CppPointerAnalysis::Options::FunctionCall:
        decorateNode(graph_, nodeId , functionNodeDecoration);
        break;

      case model::CppPointerAnalysis::Options::NullPtr:
        decorateNode(graph_, nodeId , nullptrNodeDecoration);
        break;

      case model::CppPointerAnalysis::Options::Undefined:
        decorateNode(graph_, nodeId , undefinedNodeDecoration);
        break;

      case model::CppPointerAnalysis::Options::HeapObj:
      case model::CppPointerAnalysis::Options::Literal:
        decorateNode(graph_, nodeId , objectNodeDecoration);
        break;

      default:
        break;
    }
  }
}

void PointerAnalysisDiagram::createMemorySubraphs(util::Graph& graph_)
{
  util::Graph::Subgraph heapObjContainer =
    graph_.getOrCreateSubgraph("heap");
  decorateSubgraph(graph_, heapObjContainer, heapMemorySubgraphDecoration);

  util::Graph::Subgraph stackObjContainer =
    graph_.getOrCreateSubgraph("stack");
  decorateSubgraph(graph_, stackObjContainer, stackMemorySubgraphDecoration);

  util::Graph::Subgraph globalObjContainer =
    graph_.getOrCreateSubgraph("global");
  decorateSubgraph(graph_, globalObjContainer, globalMemorySubgraphDecoration);
}

void PointerAnalysisDiagram::decorateNode(
  util::Graph& graph_,
  const util::Graph::Node& node_,
  const Decoration& decoration_) const
{
  for (const auto& attr : decoration_)
    graph_.setNodeAttribute(node_, attr.first, attr.second);
}

void PointerAnalysisDiagram::decorateSubgraph(
  util::Graph& graph_,
  const util::Graph::Subgraph& subgraph_,
  const Decoration& decoration_) const
{
  for (const auto& attr : decoration_)
    graph_.setSubgraphAttribute(subgraph_, attr.first, attr.second);
}

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::globalMemorySubgraphDecoration = {
  {"label", "Global memory"},
  {"style", "dashed"},
  {"color", "#d3d3d3"}
};
const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::stackMemorySubgraphDecoration = {
  {"label", "Stack memory"},
  {"style", "dashed"},
  {"color", "#d3d3d3"}
};
const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::heapMemorySubgraphDecoration = {
  {"label", "Heap memory"},
  {"style", "dashed"},
  {"color", "#d3d3d3"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::centerNodeDecoration = {
  {"style", "filled"},
  {"fillcolor", "gold"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::stackNodeDecoration = {
  {"shape", "Msquare"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::objectNodeDecoration = {
  {"shape", "rect"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::undefinedNodeDecoration = {
  {"shape", "rect"},
  {"style", "filled"},
  {"fillcolor", "red"},
  {"fontcolor", "white"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::nullptrNodeDecoration = {
  {"shape", "rect"},
  {"style", "filled"},
  {"fillcolor", "#ef9c9f"}
};

const PointerAnalysisDiagram::Decoration
PointerAnalysisDiagram::functionNodeDecoration = {
  {"shape", "hexagon"},
  {"style", "filled"},
  {"fillcolor", "#ded496"}
};


}
}
}
