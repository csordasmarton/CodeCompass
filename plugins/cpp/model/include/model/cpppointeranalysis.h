#ifndef CC_MODEL_CPPPOINTERANALYSIS_H
#define CC_MODEL_CPPPOINTERANALYSIS_H

#include <memory>
#include <set>

namespace cc
{
namespace model
{

#pragma db object
struct CppPointerAnalysis
{
  enum Options
  {
    HeapObj, /*!< Object which allocated on the heap: `new`, `make_shared`,
      `make_unique`. */
    StackObj, /*!< Object which allocated on the stack: `T x;` */
    GlobalObject,

    NullPtr, /*!< Null pointer: `nullptr` or `NULL`. */
    Reference, /*!< Alias to a variable or a function. For example:
      `T& x = y;`. */
    FunctionCall, /*!< Function call. */
    Return, /*!< Return statement. */
    Param,
    Member,
    Literal,
    Undefined /*!< Non initalized type, undefined memory space. */
  };

  #pragma db value
  struct StmtSide
  {
    StmtSide() = default;

    StmtSide(std::uint64_t mangledNameHash_, const std::string& operators_,
      std::set<Options> options_)
      : mangledNameHash(mangledNameHash_),
        operators(operators_),
        options(options_)
    {
    }

    std::uint64_t mangledNameHash = 0;
    std::string operators;
    std::set<Options> options;

    bool operator==(const StmtSide& rhs_) const
    {
      return mangledNameHash == rhs_.mangledNameHash;
    }

    bool operator<(const StmtSide& rhs_) const
    {
      return mangledNameHash < rhs_.mangledNameHash;
    }
  };

  #pragma db id
  int id;

  StmtSide lhs;
  StmtSide rhs;

  bool operator==(const CppPointerAnalysis& other_) const {
    return id == other_.id;
  }

#pragma db index member(lhs)
#pragma db index member(rhs)
};

typedef std::shared_ptr<CppPointerAnalysis> CppPointerAnalysisPtr;

inline bool isReference(const CppPointerAnalysis::StmtSide& side_)
{
  return side_.options.find(CppPointerAnalysis::Options::Reference) !=
    side_.options.end();
}

struct Hash
{
  size_t operator() (const CppPointerAnalysis::StmtSide& s_) const
  {
    return s_.mangledNameHash;
  }
};

} // model
} // cc

#endif // CC_MODEL_CPPPOINTERANALYSIS_H
