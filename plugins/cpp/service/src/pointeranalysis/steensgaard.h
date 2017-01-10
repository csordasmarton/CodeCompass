#ifndef CC_SERVICE_LANGUAGE_STEENSGAARD_H
#define CC_SERVICE_LANGUAGE_STEENSGAARD_H

#include <set>
#include <map>
#include <vector>
#include <memory>

#include <boost/pending/disjoint_sets.hpp>
#include <boost/property_map/property_map.hpp>

#include <model/cpppointeranalysis.h>

namespace cc
{
namespace service
{
namespace language
{

/**
 * Steensgaard points-to analysis.
 *
 * @note Steensgaard points-to algorithm is a flow-insensitive points-to
 * algorithm. It views pointer assignments as subset constraints and use these
 * constraints to propagate points-to information. Creates a subset for each
 * variable.
 * It's an imprecise algorithm but fast algorithm. Each statement needs to be
 * processed just once: nearly linear time -> O(nα(n)).
 */
class Steensgaard
{
public:

  class TypeNode;
  typedef std::shared_ptr<TypeNode> TypeNodePtr;

  class TypeNode
  {
  public:
    friend class Steensgaard;

    TypeNode(const model::CppPointerAnalysis::StmtSide& id_)
      : pointsTo(nullptr), value(id_)
    {
    }

    TypeNode(TypeNodePtr target_)
    {
      pointsTo = target_;
    }

    TypeNodePtr getTarget()
    {
      return pointsTo;
    }

    bool operator==(TypeNodePtr rhs_) const
    {
      return value == rhs_->value;
    }

    bool operator<(TypeNodePtr rhs_) const
    {
      return value < rhs_->value;
    }

    TypeNodePtr pointsTo;
    model::CppPointerAnalysis::StmtSide value;
  };

  Steensgaard();

  std::map<model::CppPointerAnalysis::StmtSide, TypeNodePtr> run(
    const std::vector<model::CppPointerAnalysis>& statements_);

private:
  /**
   * This function evaluates the left side of a statement.
   * @param lhs_ Left side of a statement.
   */
  TypeNodePtr evalLhs(const model::CppPointerAnalysis::StmtSide& lhs_);

  /**
   * This function recursively evaluates the right side of a statement.
   *
   * @param rhs_ Right side of a statement.
   * @param isDirectPointsTo_ True if the statement left side points to directly
   * to the right side in the statement.
   */
  TypeNodePtr evalRhs(
    const model::CppPointerAnalysis::StmtSide& rhs_,
    bool isDirectPointsTo_ = false);

  /**
   * The union operation merges two equivalence classes into one and returns
   * the unique representative element of the merged equivalence class.
   */
  TypeNodePtr merge(const TypeNodePtr& t1_, const TypeNodePtr& t2_);

  /**
   * Union-Find data structure.
   */
  std::map<TypeNodePtr, int> _rank;
  std::map<TypeNodePtr, TypeNodePtr> _parent;
  boost::disjoint_sets<
    boost::associative_property_map<std::map<TypeNodePtr,int>>,
    boost::associative_property_map<std::map<TypeNodePtr, TypeNodePtr>> > _ds;

  std::map<model::CppPointerAnalysis::StmtSide, TypeNodePtr> _type;
};

} // language
} // service
} // cc

#endif // CC_SERVICE_LANGUAGE_STEENSGAARD_H
