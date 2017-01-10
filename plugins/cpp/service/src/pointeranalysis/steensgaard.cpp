#include <util/logutil.h>

#include "util.h"
#include "steensgaard.h"

namespace cc
{
namespace service
{
namespace language
{

Steensgaard::Steensgaard() :
  _ds(boost::make_assoc_property_map(_rank),
      boost::make_assoc_property_map(_parent))
{
}

std::map<model::CppPointerAnalysis::StmtSide, Steensgaard::TypeNodePtr>
Steensgaard::run(const std::vector<model::CppPointerAnalysis>& statements_)
{
  std::set<model::CppPointerAnalysis::StmtSide> variables;
  for (const model::CppPointerAnalysis& statement : statements_)
  {
    variables.insert(statement.lhs);
    variables.insert(statement.rhs);
  }

  for (const model::CppPointerAnalysis::StmtSide& var : variables)
  {
    _type[var] = std::make_shared<TypeNode>(var);
    _ds.make_set(_type[var]);
  }

  for (const model::CppPointerAnalysis& statement : statements_)
  {
    // 1. Find the node n that represents the variable or variables
    // being assigned to.
    TypeNodePtr n = evalLhs(statement.lhs);

    // 2. Find the node m that represents the value being assigned.
    TypeNodePtr m = evalRhs(statement.rhs, util::isDirectPointsTo(statement));

    // 3. If node n already has an outgoing edge (to a node p != m) then merge
    // m and p; otherwise, add an edge n → m.
    if (n->pointsTo && n->pointsTo != m)
      merge(m, n->pointsTo);
    else
      n->pointsTo = m;
  }

  for (const auto& key : _type)
  {
    TypeNodePtr v = key.second;
    v->value = _ds.find_set(v)->value;
    LOG(debug) << v->value.mangledNameHash << " SET: " << _ds.find_set(v)->value.mangledNameHash;
    if (v->pointsTo)
    {
      LOG(debug) << " points-to: " << _ds.find_set(v->pointsTo)->value.mangledNameHash;
      v->pointsTo->value = _ds.find_set(v->pointsTo)->value;
    }
  }

  return _type;
}

Steensgaard::TypeNodePtr Steensgaard::evalLhs(
  const model::CppPointerAnalysis::StmtSide& lhs_)
{
  if (lhs_.operators.empty())
    return _type.at(lhs_);
  else
  {
    std::string op = lhs_.operators.substr(1);
    return evalLhs({lhs_.mangledNameHash, op, lhs_.options});
  }
}

Steensgaard::TypeNodePtr Steensgaard::evalRhs(
  const model::CppPointerAnalysis::StmtSide& rhs_,
  bool isDirectPointsTo_)
{
  if (rhs_.operators.empty())
  {
    TypeNodePtr t = _type.at(rhs_);
    return isDirectPointsTo_ ? t : t->getTarget();
  }
  else if (rhs_.operators[0] == '&')
  {
    std::string op = rhs_.operators.substr(1);

    if (op.empty())
      return _type.at(rhs_);

    return evalRhs({rhs_.mangledNameHash, op, rhs_.options}, true);
  }
  else
  {
    std::string op = rhs_.operators.substr(1);
    TypeNodePtr t = _type[rhs_]->getTarget();
    return evalRhs({t->value.mangledNameHash, op, rhs_.options},
      isDirectPointsTo_);
  }
}

Steensgaard::TypeNodePtr Steensgaard::merge(
  const TypeNodePtr& t1_,
  const TypeNodePtr& t2_)
{
  if (!t1_ && !t2_)
    return nullptr;

  if (!t1_) return t2_;
  if (!t2_ || t1_ == t2_) return t1_;

  _ds.union_set(t1_, t2_);

  TypeNodePtr u = _ds.find_set(t1_);
  u->pointsTo = merge(t1_->pointsTo, t2_->pointsTo);

  return u;
}

}
}
}
