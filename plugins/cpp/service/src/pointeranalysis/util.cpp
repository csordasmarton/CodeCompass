#include <set>
#include <algorithm>

#include "util.h"

namespace cc
{
namespace util
{

bool isDirectPointsTo(const model::CppPointerAnalysis stmt_)
{
  std::set<model::CppPointerAnalysis::Options> rhsOpt = stmt_.rhs.options;
  return model::isReference(stmt_.lhs) ||
    std::any_of(rhsOpt.begin(), rhsOpt.end(), [] (
      model::CppPointerAnalysis::Options opt_)
    {
      return
        opt_ == model::CppPointerAnalysis::Options::NullPtr ||
        opt_ == model::CppPointerAnalysis::Options::HeapObj ||
        opt_ == model::CppPointerAnalysis::Options::Undefined ||
        opt_ == model::CppPointerAnalysis::Options::Literal ||
        opt_ == model::CppPointerAnalysis::Options::FunctionCall;
    });
}

bool isSimpleConstraint(const model::CppPointerAnalysis stmt_)
{
  return stmt_.lhs.operators.empty() &&
        !stmt_.rhs.operators.empty() &&
         stmt_.rhs.operators == "&";
}

}
}
