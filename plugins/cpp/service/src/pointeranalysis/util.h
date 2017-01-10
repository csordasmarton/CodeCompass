#include <model/cpppointeranalysis.h>

namespace cc
{
namespace util
{

/**
 * Return true if the statement satisfies the simple constraint condition.
 * @note E.g.: x = &b;
 */
bool isSimpleConstraint(const model::CppPointerAnalysis stmt_);

/**
 * Return true if the statement left side points to directly to the right side
 * of the statement. It happens in the folling cases:
 *  - Referencing: a = &b;
 *  - Create a reference (alias): &a = b;
 */
bool isDirectPointsTo(const model::CppPointerAnalysis stmt_);

}
}
