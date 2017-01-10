#ifndef CC_PARSER_POINTERANALYSISCOLLECTOR_H
#define CC_PARSER_POINTERANALYSISCOLLECTOR_H

#include <vector>

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Decl.h>

#include <model/cppastnode.h>
#include <model/cppastnode-odb.hxx>
#include <model/cpppointeranalysis.h>
#include <model/cpppointeranalysis-odb.hxx>

#include <parser/parsercontext.h>
#include <parser/sourcemanager.h>

#include <util/odbtransaction.h>

#include "symbolhelper.h"
#include "manglednamecache.h"
#include "idcache.h"
#include "filelocutil.h"

namespace
{

/**
 * List of smart pointers.
 */
const std::vector<std::string> smartPointers =
{
  "std::shared_ptr", "std::unique_ptr", "std::auto_ptr", "std::weak_ptr"
};

const std::vector<std::string> allocators =
{
  "malloc", "calloc", "realloc"
};

/**
 * Determine whether constructor declaration is a smart pointer.
 * @param cd_ C++ constructor declaration.
 * @return True if the
 */
bool isSmartPointer(const std::string& name_)
{
  for (const std::string smartPtr : smartPointers)
    if (name_.find(smartPtr) != std::string::npos)
      return true;
  return false;
}

/**
 * This function checks if the function declaration is a move operator.
 * @return True if the declaration is a move operator.
 */
bool isMoveOperator(const clang::FunctionDecl* fd_)
{
  const clang::NamedDecl* namedCallee = llvm::dyn_cast<clang::NamedDecl>(fd_);
  return namedCallee->getNameAsString() == "move";
}

bool isAllocatorCall(const clang::FunctionDecl* fd_)
{
  const clang::NamedDecl* namedCallee = llvm::dyn_cast<clang::NamedDecl>(fd_);
  std::string name = namedCallee->getNameAsString();
  return
    std::find(allocators.begin(), allocators.end(), name)!= allocators.end();
}

std::set<cc::model::CppPointerAnalysis::Options> getVariableOptions(
  const clang::VarDecl* vd_)
{
  std::set<cc::model::CppPointerAnalysis::Options> options;
  const clang::Type* type = vd_->getType().getTypePtrOrNull();
  if (type && type->isReferenceType())
    options.insert(cc::model::CppPointerAnalysis::Options::Reference);

  bool isParam = llvm::isa<clang::ParmVarDecl>(vd_);
  if (isParam)
    options.insert(cc::model::CppPointerAnalysis::Options::Param);

  if (isParam || vd_->isLocalVarDecl())
    options.insert(cc::model::CppPointerAnalysis::Options::StackObj);
  else
    options.insert(cc::model::CppPointerAnalysis::Options::GlobalObject);

  if (vd_->isStaticLocal() || vd_->isStaticDataMember())
    options.insert(cc::model::CppPointerAnalysis::Options::GlobalObject);

  return options;
}

std::string getSuffixFromLoc(const cc::model::FileLoc& fileLoc_)
{
  if (!fileLoc_.file)
    return std::string();

  return std::to_string(fileLoc_.file.object_id()) + ':'
       + std::to_string(fileLoc_.range.start.line) + ':'
       + std::to_string(fileLoc_.range.start.column);
}

bool isPointerOrReferenceType(
  const clang::Type* type,
  const std::string& sideType_)
{
  if (type && (
      type->isAnyPointerType() ||
      type->isReferenceType() ||
      type->isArrayType() ||
      isSmartPointer(sideType_)))
    return true;
  return false;
}

class AstNodeCreator
{
public:
  AstNodeCreator(
    cc::parser::SourceManager& srcMgr_,
    const clang::ASTContext& astContext_)
    : _srcMgr(srcMgr_),
      _fileLocUtil(astContext_.getSourceManager()),
      _cppSourceType("CPP")
  {
  }

  cc::model::CppAstNodePtr operator()(
    const std::string& astValue_,
    const std::string& mangledName_,
    const clang::SourceLocation& start_,
    const clang::SourceLocation& end_,
    bool addSuffixToMangledName_ = true)
  {
    cc::model::CppAstNodePtr astNode = std::make_shared<cc::model::CppAstNode>();
    astNode->symbolType = cc::model::CppAstNode::SymbolType::Other;
    astNode->astType = cc::model::CppAstNode::AstType::Other;
    astNode->visibleInSourceCode = false;
    astNode->astValue = astValue_;

    if (!addFileLoc(astNode, start_, end_))
      return nullptr;

    astNode->mangledName = mangledName_;

    if (addSuffixToMangledName_ && start_.isValid() && end_.isValid())
      astNode->mangledName += getSuffixFromLoc(astNode->location);

    astNode->mangledNameHash = cc::util::fnvHash(astNode->mangledName);
    astNode->id = cc::model::createIdentifier(*astNode);

    return astNode;
  }

private:

  bool addFileLoc(
    cc::model::CppAstNodePtr& astNode_,
    const clang::SourceLocation& start_,
    const clang::SourceLocation& end_)
  {
    if (start_.isInvalid() || end_.isInvalid())
      return false;

    cc::model::FileLoc fileLoc;
    _fileLocUtil.setRange(start_, end_, fileLoc.range);
    fileLoc.file = _srcMgr.getFile(_fileLocUtil.getFilePath(start_));

    const std::string& type = fileLoc.file.load()->type;
    if (type != cc::model::File::DIRECTORY_TYPE && type != _cppSourceType)
    {
      fileLoc.file->type = _cppSourceType;
      _srcMgr.updateFile(*fileLoc.file);
    }

    astNode_->location = fileLoc;
    return true;
  }

  cc::parser::SourceManager& _srcMgr;
  cc::parser::FileLocUtil _fileLocUtil;
  const std::string _cppSourceType;
};

}

namespace cc
{
namespace parser
{


/**
 * TODO
 */
class ReturnCollector : public clang::RecursiveASTVisitor<ReturnCollector>
{
public:
  ReturnCollector( std::unordered_set<clang::Expr*>& collected_)
    : _collected(collected_)
  {
  }

  void collect(clang::Stmt* stmt_)
  {
    this->TraverseStmt(stmt_);
  }

  void collect(clang::Decl* decl_)
  {
    this->TraverseDecl(decl_);
  }

  // TODO: xerxes failed here.
  bool VisitReturnStmt(clang::ReturnStmt* rs_)
  {
    clang::Expr* retValue = rs_->getRetValue();
    if (retValue && (
        llvm::isa<clang::CXXNullPtrLiteralExpr>(retValue) ||
        llvm::isa<clang::GNUNullExpr>(retValue) ||
        llvm::isa<clang::CXXConstructExpr>(retValue) ||
        llvm::isa<clang::CXXNewExpr>(retValue) ||
        llvm::isa<clang::DeclRefExpr>(retValue)
        ))
    {
      _collected.insert(retValue);
    }

    return true;
  }

private:
  std::unordered_set<clang::Expr*>& _collected;
};

// http://dimitar-asenov.github.io/Envision/classCppImport_1_1ExpressionVisitor.html
class StmtCollector : public clang::RecursiveASTVisitor<StmtCollector>
{
public:
  StmtCollector(
    std::set<model::CppPointerAnalysis::StmtSide>& collected_,
    ParserContext& ctx_,
    clang::ASTContext& astContext_,
    MangledNameCache& mangledNameCache_,
    std::unordered_map<const void*, model::CppAstNodeId>& clangToAstNodeId_,
    std::vector<model::CppAstNodePtr>& astNodes_)
    : _collected(collected_),
      _ctx(ctx_),
      _astContext(astContext_),
      _mangledNameCache(mangledNameCache_),
      _clangToAstNodeId(clangToAstNodeId_),
      _astNodes(astNodes_),
      _clangSrcMgr(astContext_.getSourceManager()),
      _astNodeCreator(ctx_.srcMgr, astContext_),
      _fileLocUtil(astContext_.getSourceManager()),
      _mngCtx(astContext_.createMangleContext()),
      _shouldCollect(true),
      _cppSourceType("CPP"),
      _operators(""),
      _isReturnType(false),
      _returnCollectorCallCount(0)
  {
  }

  void collect(clang::Stmt* stmt_)
  {
    _startStmt = stmt_;
    this->TraverseStmt(_startStmt);
  }

  void collect(clang::Decl* decl_)
  {
    _startDecl = decl_;
    this->TraverseDecl(_startDecl);
  }

//  bool VisitIntegerLiteral(clang::IntegerLiteral* il_)
//  {
//    std::string value = getSourceText(il_->getLocStart(), il_->getLocEnd());
//    std::uint64_t mangledNameHash = createAstNode(value, value,
//      il_->getLocStart(), il_->getLocEnd());
//
//    addStmtSide(mangledNameHash, _operators, {
//      model::CppPointerAnalysis::Options::Literal});
//
//    return false;
//  }

  bool VisitStringLiteral(clang::StringLiteral* sl_)
  {
    std::string value = getSourceText(sl_->getLocStart(), sl_->getLocEnd());
    std::uint64_t mangledNameHash = createAstNode(value, value,
      sl_->getLocStart(), sl_->getLocEnd());

    addStmtSide(mangledNameHash, _operators, {
      model::CppPointerAnalysis::Options::Literal,
      model::CppPointerAnalysis::Options::GlobalObject});

    return false;
  }

  /**
   * This function visits the `nullptr` literal and creates an AST node for it
   * with a unique id.
   */
  bool VisitCXXNullPtrLiteralExpr(clang::CXXNullPtrLiteralExpr* ne_)
  {
    std::uint64_t mangledNameHash = createAstNode("nullptr", "nullptr",
      ne_->getLocStart(), ne_->getLocEnd());

    addStmtSide(mangledNameHash, _operators, {
      model::CppPointerAnalysis::Options::NullPtr});

    return false;
  }

  bool TraverseGNUNullExpr(clang::GNUNullExpr* ne_)
  {
    std::uint64_t mangledNameHash = createAstNode("NULL", "NULL",
      ne_->getLocStart(), ne_->getLocEnd());

    addStmtSide(mangledNameHash, _operators, {
      model::CppPointerAnalysis::Options::NullPtr});

    return false;
  }

  /**
   * Traverse dereference (`*`) operator
   */
  bool TraverseUnaryDeref(clang::UnaryOperator* uop_)
  {
    _operators += "*";
    RecursiveASTVisitor<StmtCollector>::TraverseUnaryAddrOf(uop_);

    return true;
  }

  /**
   * Traverse address of (`&`) operator.
   */
  bool TraverseUnaryAddrOf(clang::UnaryOperator* uop_)
  {
    _operators += "&";
    RecursiveASTVisitor<StmtCollector>::TraverseUnaryAddrOf(uop_);

    return true;
  }

  /**
   * Visit variable declaration. If the variable is a reference type, adds an
   * address of operator (`&`) to _operators.
   */
  bool VisitVarDecl(clang::VarDecl* vd_)
  {
    addStmtSide(getStmtMangledName(vd_), _operators, getVariableOptions(vd_));

    return false;
  }

  bool VisitCXXConstructExpr(clang::CXXConstructExpr* ce_)
  {
    const clang::CXXConstructorDecl* ctor = ce_->getConstructor();

    if (isSmartPointer(ce_->getType().getAsString()))
    {
      if (ce_->getNumArgs())
      {
        clang::Expr* init = ce_->getArg(0);

        this->TraverseStmt(init);
        return false;
      }
      else
      {
        std::uint64_t mangledNameHash = createAstNode("nullptr", "nullptr",
          ce_->getLocStart(), ce_->getLocEnd());
        addStmtSide(mangledNameHash, _operators, {
          model::CppPointerAnalysis::Options::NullPtr});
        return false;
      }
    }

    std::uint64_t mangledNameHash = createAstNode(ctor->getNameAsString(),
      getMangledName(_mngCtx, ctor), ce_->getLocStart(), ce_->getLocEnd());
    addStmtSide(mangledNameHash, _operators, {
      model::CppPointerAnalysis::Options::StackObj});

    return false;
  }

  bool VisitCXXNewExpr(clang::CXXNewExpr* ne_)
//    clang::Expr* init = ne_->getInitializer();
  {
//    if (init && llvm::isa<clang::CXXConstructExpr>(init))
//    {
//      clang::CXXConstructExpr* ce =
//        llvm::dyn_cast<clang::CXXConstructExpr>(init);
//     RecursiveASTVisitor<StmtCollector>::TraverseCXXConstructExpr(ce);
//
//     _stmtSide.options.insert(
//       model::CppPointerAnalysis::Options::HeapObj);
//
//     return false;
//    }

    const clang::FunctionDecl* newDecl = ne_->getOperatorNew();

    if (!newDecl)
      return false;

    std::string astValue = getSourceText(ne_->getLocStart(), ne_->getLocEnd());
    if (astValue.empty())
      astValue = newDecl->getNameAsString();

    std::uint64_t mangledNameHash = createAstNode(
      astValue, getMangledName(_mngCtx, newDecl),
      ne_->getLocStart(), ne_->getLocEnd());

    addStmtSide(mangledNameHash, _operators, {
      model::CppPointerAnalysis::Options::HeapObj});

    return false;
  }

  bool VisitExprWithCleanups(clang::ExprWithCleanups* ec_)
  {
    addStmtSide(getStmtMangledName(ec_), _operators);
    return false;
  }

  bool VisitCallExpr(clang::CallExpr* ce_)
  {
    clang::FunctionDecl* callee = ce_->getDirectCallee();

    // TODO: If call is a function pointer call, callee is a nullptr

    if (!callee)
    {
//      clang::Decl* decl = ce_->getCalleeDecl();
//      _startStmt = ce_;
//
//      this->TraverseDecl(decl);
      return false;
    }

    //--- Check if it's a `move` operator ---//

    if (isMoveOperator(callee) && ce_->getNumArgs())
    {
      this->TraverseStmt(ce_->getArg(0));
      return false;
    }

    std::unordered_set<clang::Expr*> ret;
    ReturnCollector collector(ret);
    collector.collect(callee);

    ++_returnCollectorCallCount;

    if (ret.empty() || _returnCollectorCallCount > _maxReturnCount ||
        isAllocatorCall(callee))
    {
      addStmtSide(getStmtMangledName(ce_), _operators, {
        model::CppPointerAnalysis::Options::FunctionCall});
      return false;
    }

    _isReturnType = true;
    std::string operators = _operators;
    for (clang::Expr* expr : ret)
    {
      _operators = operators;
      this->TraverseStmt(expr);
    }

    return false;
  }

  bool VisitMemberExpr(clang::MemberExpr* me_)
  {
//    const clang::ValueDecl* mDecl = me_->getMemberDecl();
//
//    std::uint64_t mangledNameHash =
//    clang::Expr* base = me_->getBase();
//    if (const clang::DeclRefExpr* dr = llvm::dyn_cast<clang::DeclRefExpr>(base))
//    {
//      const clang::ValueDecl* decl = dr->getDecl();
//      if (const clang::VarDecl* vd = llvm::dyn_cast<clang::VarDecl>(decl))
//      {
//        std::string astValue = vd->getNameAsString()
//          + (me_->isArrow() ? "->" : ".") + mDecl->getNameAsString();
//
//        _stmtSide.mangledNameHash = createAstNode(astValue,
//          getMangledName(_mngCtx, mDecl) + getMangledName(_mngCtx, vd),
//          me_->getLocStart(), me_->getLocEnd(), false);
//      }
//
//    }

    addStmtSide(getStmtMangledName(me_), _operators, {
      model::CppPointerAnalysis::Options::Member});

    return false;
  }

  bool VisitDeclRefExpr(clang::DeclRefExpr* re_)
  {
    clang::ValueDecl* decl = re_->getDecl();

    std::set<cc::model::CppPointerAnalysis::Options> options;
    if (const clang::VarDecl* vd = llvm::dyn_cast<clang::VarDecl>(decl))
      options = getVariableOptions(vd);

    addStmtSide(getStmtMangledName(re_), _operators, options);
    return false;
  }

  bool VisitFieldDecl(clang::FieldDecl* fd_)
  {
    addStmtSide(getStmtMangledName(fd_), _operators);
    return false;
  }

private:
  void addStmtSide(
    std::uint64_t mangledNameHash_,
    const std::string operators_,
    std::set<model::CppPointerAnalysis::Options> options_ = {})
  {
    if (_isReturnType)
      options_.insert(model::CppPointerAnalysis::Options::Return);

    _collected.insert({mangledNameHash_, operators_, options_});
  }

  /**
   * Returns a string for the source that the range encompasses.
   */
  std::string getSourceText(
    const clang::SourceLocation& begin_,
    const clang::SourceLocation& end_)
  {
    clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(
    _clangSrcMgr.getSpellingLoc(begin_), _clangSrcMgr.getSpellingLoc(end_));

    if (range.isInvalid())
      return std::string();

    clang::LangOptions langOpts;
    clang::StringRef src =
      clang::Lexer::getSourceText(range, _clangSrcMgr, langOpts);

    return src.str();
  }

  std::uint64_t createAstNode(
    const std::string& astValue_,
    const std::string& mangledName_,
    const clang::SourceLocation& start_,
    const clang::SourceLocation& end_,
    bool addSuffixToMangledName_ = true)
  {
    cc::model::CppAstNodePtr astNode = _astNodeCreator(astValue_, mangledName_,
      start_, end_, addSuffixToMangledName_);

    if (astNode)
    {
      if (_mangledNameCache.insert(*astNode))
        _astNodes.push_back(astNode);

      return astNode->mangledNameHash;
    }
    return 0;
  }

  template <typename T>
  std::uint64_t getStmtMangledName(T* t_)
  {
    auto it = _clangToAstNodeId.find(t_);
    if (it != _clangToAstNodeId.end())
      return _mangledNameCache.at(it->second);

    return 0;
  }

  union
  {
    clang::Stmt* _startStmt;
    clang::Decl* _startDecl;
  };

  const int _maxReturnCount = 5;

  std::set<model::CppPointerAnalysis::StmtSide>& _collected;
  ParserContext& _ctx;
  clang::ASTContext& _astContext;
  MangledNameCache& _mangledNameCache;
  std::unordered_map<const void*, model::CppAstNodeId>& _clangToAstNodeId;
  std::vector<model::CppAstNodePtr>& _astNodes;
  const clang::SourceManager& _clangSrcMgr;
  AstNodeCreator _astNodeCreator;
  FileLocUtil _fileLocUtil;
  clang::MangleContext* _mngCtx;
  bool _shouldCollect;
  const std::string _cppSourceType;
  std::string _operators;
  bool _isReturnType;
  int _returnCollectorCallCount;
};

class PointerAnalysisCollector
  : public clang::RecursiveASTVisitor<PointerAnalysisCollector>
{
public:
  PointerAnalysisCollector(
    ParserContext& ctx_,
    clang::ASTContext& astContext_,
    MangledNameCache& mangledNameCache_,
    IdCache& pointerAnalysisCache_,
    std::unordered_map<const void*, model::CppAstNodeId>& clangToAstNodeId_)
    : _ctx(ctx_),
      _astContext(astContext_),
      _mangledNameCache(mangledNameCache_),
      _pointerAnalysisCache(pointerAnalysisCache_),
      _clangToAstNodeId(clangToAstNodeId_),
      _astNodeCreator(ctx_.srcMgr, astContext_)
  {
  }

  ~PointerAnalysisCollector()
  {
    (util::OdbTransaction(_ctx.db))([this]{
      for (const model::CppAstNodePtr& astNode : _astNodes)
        _ctx.db->persist(*astNode);

      for (model::CppPointerAnalysis& pAnalysis : _pAnalysis)
        _ctx.db->persist(pAnalysis);
    });
  }

  bool VisitBinaryOperator(clang::BinaryOperator* bop_)
  {
    if (bop_->isAssignmentOp() ||
        bop_->isCompoundAssignmentOp() ||
        bop_->isShiftAssignOp())
    {
      clang::Expr* lhs = bop_->getLHS();
      const clang::Type* type = lhs->getType().getTypePtrOrNull();
      if (!isPointerOrReferenceType(type, lhs->getType().getAsString()))
        return true;

      makeAssignRels(bop_->getLHS(), bop_->getRHS());
    }

    return true;
  }

  /**
   * Visit variable declarations.
   * E.g. (1.) T* x1; (2.) T* x2 = new T(); (3.) T* x3 = x1;
   * @note: For function parameter which are not being initialized getInit()
   * function will returns a null pointer.
   */
  bool VisitVarDecl(clang::VarDecl* vd_)
  {
    const clang::Type* type = vd_->getType().getTypePtrOrNull();
    if (!isPointerOrReferenceType(type, vd_->getType().getAsString()))
      return true;

    clang::Expr* init = vd_->getInit();
    if (!init && !llvm::isa<clang::ParmVarDecl>(vd_))
      makeUndefinedRels(vd_);
    else
      makeAssignRels(vd_, init);

    return true;
  }

  bool VisitCallExpr(clang::CallExpr* ce_)
  {
    clang::FunctionDecl* callee = ce_->getDirectCallee();

    if (!callee)
      return true;

    for (std::size_t i = 0;
         i < callee->getNumParams() && i < ce_->getNumArgs();
         ++i)
      makeAssignRels(callee->getParamDecl(i), ce_->getArg(i));

    return true;
  }

  /**
   * Creates statements for constructor initializers.
   */
  bool VisitCXXConstructorDecl(clang::CXXConstructorDecl* cd_)
  {
    for (auto it = cd_->init_begin(); it != cd_->init_end(); ++it)
    {
      clang::CXXCtorInitializer* init = *it;
      clang::FieldDecl* member = init->getMember();

      if (!member || init->getSourceOrder() == -1)
        continue;

      makeAssignRels(member, init->getInit());
    }

    return true;
  }

private:

  template <typename T>
  std::set<model::CppPointerAnalysis::StmtSide> collect(T* s_)
  {
    std::set<model::CppPointerAnalysis::StmtSide> ret;

    StmtCollector rc(ret, _ctx, _astContext, _mangledNameCache,
      _clangToAstNodeId, _astNodes);

    rc.collect(s_);

    return ret;
  }

  void createPointerAnalysis(
    const std::set<model::CppPointerAnalysis::StmtSide>& lhs_,
    const std::set<model::CppPointerAnalysis::StmtSide>& rhs_)
  {
    for (const model::CppPointerAnalysis::StmtSide& lhs : lhs_)
      for (const model::CppPointerAnalysis::StmtSide& rhs : rhs_)
      {
        if (lhs.mangledNameHash && rhs.mangledNameHash)
        {
          std::string id = std::to_string(lhs.mangledNameHash) +
            std::to_string(rhs.mangledNameHash);

          model::CppPointerAnalysis pAnalysis;
          pAnalysis.lhs = lhs;
          pAnalysis.rhs = rhs;

          pAnalysis.id = util::fnvHash(id);

          if (_pointerAnalysisCache.insert(pAnalysis.id ))
            _pAnalysis.push_back(std::move(pAnalysis));
        }
      }
  }

  std::uint64_t createAstNode(
    const std::string& astValue_,
    const std::string& mangledName_,
    const clang::SourceLocation& start_,
    const clang::SourceLocation& end_,
    bool addSuffixToMangledName_ = true)
  {
    cc::model::CppAstNodePtr astNode = _astNodeCreator(astValue_, mangledName_,
      start_, end_, addSuffixToMangledName_);

    if (astNode)
    {
      if (_mangledNameCache.insert(*astNode))
        _astNodes.push_back(astNode);

      return astNode->mangledNameHash;
    }
    return 0;
  }

  void makeUndefinedRels(clang::VarDecl* lhs_)
  {
    std::set<model::CppPointerAnalysis::StmtSide> lhs = collect(lhs_);
    model::CppPointerAnalysis::StmtSide rhs;
    rhs.mangledNameHash = createAstNode("undefined", "undefined",
      lhs_->getLocStart(), lhs_->getLocEnd());
    rhs.options.insert(
      model::CppPointerAnalysis::Options::Undefined);
    createPointerAnalysis(lhs, {rhs});
  }

  template <typename T1, typename T2>
  void makeAssignRels(T1* leftSide_, T2* rightSide_)
  {
    std::set<model::CppPointerAnalysis::StmtSide> lhs = collect(leftSide_);
    std::set<model::CppPointerAnalysis::StmtSide> rhs = collect(rightSide_);

    createPointerAnalysis(lhs, rhs);
  }

  std::vector<model::CppAstNodePtr>      _astNodes;
  std::vector<model::CppPointerAnalysis> _pAnalysis;

  ParserContext& _ctx;
  clang::ASTContext& _astContext;
  MangledNameCache& _mangledNameCache;
  IdCache& _pointerAnalysisCache;
  std::unordered_map<const void*, model::CppAstNodeId>& _clangToAstNodeId;
  AstNodeCreator _astNodeCreator;
};

} // parser
} // cc

#endif // CC_PARSER_POINTERANALYSISCOLLECTOR_H
