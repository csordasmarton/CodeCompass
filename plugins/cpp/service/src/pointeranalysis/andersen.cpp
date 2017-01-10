#include <util/logutil.h>

#include "util.h"
#include "andersen.h"

namespace cc
{
namespace service
{
namespace language
{

std::vector<model::CppPointerAnalysis> Andersen::init(
  const std::vector<model::CppPointerAnalysis>& statements_)
{
  std::vector<model::CppPointerAnalysis> complexStmts;

  for (const model::CppPointerAnalysis& state : statements_)
  {
    if (util::isSimpleConstraint(state))
      _PT[state.lhs].insert(state.rhs);
    else
      complexStmts.push_back(state);
  }
  return complexStmts;
}

Andersen::PointsToSet Andersen::run(
  const std::vector<model::CppPointerAnalysis>& statements_)
{
  //--- Initalize the algorithm ---//

  std::vector<model::CppPointerAnalysis> complexStmts = init(statements_);

  //--- Run algorithm on the complex statements ---//

  bool changed = !complexStmts.empty();
  while (changed)
  {
    changed = false;
    for (const model::CppPointerAnalysis& state : complexStmts)
    {
      for (const model::CppPointerAnalysis::StmtSide& lhs :evalLHS(state.lhs))
      {
        for (const model::CppPointerAnalysis::StmtSide& rhs : evalRHS(
          state.rhs, util::isDirectPointsTo(state)))
        {
          changed = !changed && _PT[lhs].find(rhs) == _PT[lhs].end();
          _PT[lhs].insert(rhs);
        }
      }
    }
  };

  return _PT;
}

std::set<model::CppPointerAnalysis::StmtSide> Andersen::evalLHS(
  const model::CppPointerAnalysis::StmtSide& lhs_)
{
  std::set<model::CppPointerAnalysis::StmtSide> ret;

  if (lhs_.operators.empty())
  {
    ret.insert(lhs_);
  }
  else if (lhs_.operators[0] == '*')
  {
    std::string operators = lhs_.operators.substr(1);
    return evalRHS(model::CppPointerAnalysis::StmtSide{
      lhs_.mangledNameHash, operators, lhs_.options});
  }

  return ret;
}

std::set<model::CppPointerAnalysis::StmtSide> Andersen::evalRHS(
  const model::CppPointerAnalysis::StmtSide& rhs_,
  bool isDirectPointsTo_)
{
  if (rhs_.operators.empty())
  {
    if (isDirectPointsTo_)
      return {rhs_};
    else
      return _PT[rhs_];
  }
  else if (rhs_.operators[0] == '&')
  {
    std::string operators = rhs_.operators.substr(1);

    if (operators.empty())
      return {rhs_};

    return evalRHS(model::CppPointerAnalysis::StmtSide{
      rhs_.mangledNameHash, operators, rhs_.options}, true);
  }
  else
  {
    std::set<model::CppPointerAnalysis::StmtSide> ret;
    std::string operators = rhs_.operators.substr(1);
    for (const model::CppPointerAnalysis::StmtSide& e : _PT[rhs_])
    {
      for (const model::CppPointerAnalysis::StmtSide& stmtSide : evalRHS(
        model::CppPointerAnalysis::StmtSide{e.mangledNameHash, operators,
        e.options}, isDirectPointsTo_))
      {
        ret.insert(stmtSide);
      }
    }
    return ret;
  }
}

}
}
}
