/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file ClauseCodeTree.hpp
 * Defines class ClauseCodeTree.
 */

#ifndef __ClauseCodeTree__
#define __ClauseCodeTree__

#include "Forwards.hpp"

#include "Lib/Allocator.hpp"
#include "Lib/DArray.hpp"
#include "Lib/Stack.hpp"

#include "CodeTree.hpp"


namespace Indexing {

using namespace Lib;
using namespace Kernel;

template<bool higherOrder>
class ClauseCodeTree : public CodeTree
{
protected:
  void onCodeOpDestroying(CodeOp* op) override;
  void printSuccess(std::ostream& out, const CodeOp& op) const override;
  
public:
  ClauseCodeTree();

  void insert(Clause* cl);
  void remove(Clause* cl);

protected:

  //////// insertion //////////

  struct InitialLiteralOrderingComparator;

  void optimizeLiteralOrder(DArray<Literal*>& lits);
  void evalSharing(Literal* lit, CodeOp* startOp, size_t& sharedLen, size_t& unsharedLen, CodeOp*& nextOp);
  static void matchCode(CodeStack& code, CodeOp* startOp, size_t& matchedCnt, CodeOp*& nextOp);

  //////// removal //////////

  bool removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks);

  struct RemovingLiteralMatcher
  : public Matcher</*removing*/true,false,higherOrder>
  {
    using Base = Matcher</*removing*/true,false,higherOrder>;

    void init(CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_,
	    const ClauseCodeTree& tree_, Stack<CodeOp*>* firstsInBlocks_);

    USE_ALLOCATOR(RemovingLiteralMatcher);
  };

  //////// retrieval //////////

  /** Context for finding matches of literals
   *
   * Here the actual execution of the code of the tree takes place */
  struct LiteralMatcher
  : public Matcher</*removing*/false,false,higherOrder>
  {
    using Base = Matcher</*removing*/false,false,higherOrder>;
    using Base::op;
    using Base::_matched;
    using Base::finished;
    using Base::execute;

    void init(const CodeTree& tree, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, bool seekOnlySuccess=false);
    bool next();
    bool doEagerMatching();

    inline bool eagerlyMatched() const { return _eagerlyMatched; }

    inline ILStruct* getILS() { ASS(Base::matched()); return op->getILS(); }

    USE_ALLOCATOR(LiteralMatcher);

  private:
    bool _eagerlyMatched;

    Stack<CodeOp*> eagerResults;

    void recordMatch();
  };

public:
  struct ClauseMatcher
  {
    void init(ClauseCodeTree* tree_, Clause* query_, bool sres_);
    void reset();
    bool keepRecycled() const { return lInfos.keepRecycled(); }

    Clause* next(int& resolvedQueryLit);

    bool matched() { return lms.isNonEmpty() && lms.top()->success(); }
    CodeOp* getSuccessOp() { ASS(matched()); return lms.top()->op; }

    USE_ALLOCATOR(ClauseMatcher);

  private:
    void enterLiteral(CodeOp* entry, bool seekOnlySuccess);
    void leaveLiteral();
    bool canEnterLiteral(CodeOp* op);

    bool checkCandidate(Clause* cl, int& resolvedQueryLit);
    bool matchGlobalVars(int& resolvedQueryLit);
    bool compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq);

    bool existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* oi);

    Clause* query;
    ClauseCodeTree* tree;
    bool sres;

    static const unsigned sresNoLiteral=static_cast<unsigned>(-1);
    unsigned sresLiteral;

    /**
     * Literal infos that we will attempt to match
     * For each equality we add two lit infos, once with reversed arguments.
     * The order of infos is:
     *
     * Ground literals
     * Non-ground literals
     * [if sres] Ground literals negated
     * [if sres] Non-ground literals negated
     */
    DArray<LitInfo> lInfos;

    Stack<Recycled<LiteralMatcher, NoReset>> lms;
  };

protected:

  //////// member variables //////////

#if VDEBUG
  unsigned _clauseMatcherCounter;
#endif

};

template<bool higherOrder>
class OptimizedClauseCodeTree
: public ClauseCodeTree<higherOrder>
{
  using Base = ClauseCodeTree<higherOrder>;
  using ILStruct = CodeTree::ILStruct;
  using CodeOp = CodeTree::CodeOp;
  using LitInfo = CodeTree::LitInfo;
  using MatchInfo = CodeTree::MatchInfo;
  using CodeBlock = CodeTree::CodeBlock;
  using CodeStack = CodeTree::CodeStack;
  using SearchStruct = CodeTree::SearchStruct;
  using Base::matchCode;

public:
  OptimizedClauseCodeTree() : Base() {}

  using Base::incorporate;
  using Base::getEntryPoint;

  void insert(Clause* cl);
  void remove(Clause* cl);
  void incorporate(CodeTree::CodeStack& code, ILStruct** matchedIls);

protected:

  /** Context for finding matches of literals. */
  struct OptimizedLiteralMatcher
  : public CodeTree::Matcher</*removing*/false,false,higherOrder>
  {
    using Base = CodeTree::Matcher</*removing*/false,false,higherOrder>;
    using Base::op;
    using Base::_matched;
    using Base::matched;
    using Base::success;
    using Base::finished;
    using Base::execute;

    void init(OptimizedClauseCodeTree* tree, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, unsigned depth_,
	    unsigned lmsIndex_, Stack<CodeOp*>* futureCache_, CodeOp* prevLitEnd_, bool seekOnlySuccess=false);
    bool next();
    bool doEagerMatching();

    inline bool eagerlyMatched() const { return _eagerlyMatched; }

    inline ILStruct* getILS() { ASS(this->matched()); return op->getILS(); }

    USE_ALLOCATOR(OptimizedLiteralMatcher);

  private:
    bool _eagerlyMatched;
    unsigned depth;
    unsigned lmsIndex;

    /** Cache of LIT_END ops of higher depth encountered while matching this
     * (or an ancestor) literal, shared with all literal matchers of the
     * enclosing OptimizedClauseMatcher. A literal matcher entered right after
     * a LIT_END op picks its own matches out of this cache -- those whose
     * ILStruct::previous is the ILStruct of that LIT_END op -- before trying
     * any actual code. */
    Stack<CodeOp*>* futureCache;
    /** The LIT_END op executed by the previous literal matcher that led us
     * here, or nullptr for the very first literal. Used to find this
     * literal's own entries in @b futureCache. */
    CodeOp* prevLitEnd;

    Stack<CodeOp*> eagerResults;

    void recordMatch();
    CodeOp* pickFutureCacheEntry();
  };

public:
  struct OptimizedClauseMatcher
  {
    void init(OptimizedClauseCodeTree* tree_, Clause* query_, bool sres_);
    void reset();
    bool keepRecycled() const { return lInfos.keepRecycled(); }

    Clause* next(int& resolvedQueryLit);

    bool matched() { return lms.isNonEmpty() && lms.top()->success(); }
    CodeOp* getSuccessOp() { ASS(matched()); return lms.top()->op; }

    USE_ALLOCATOR(OptimizedClauseMatcher);

  private:
    void enterLiteral(CodeOp* entry, unsigned depth, bool seekOnlySuccess, CodeOp* prevLitEnd);
    void leaveLiteral();
    bool canEnterLiteral(CodeOp* op);

    bool checkCandidate(Clause* cl, int& resolvedQueryLit);
    bool matchGlobalVars(int& resolvedQueryLit);
    bool compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq);

    bool existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* oi);

    Clause* query;
    OptimizedClauseCodeTree* tree;
    bool sres;

    /** Cache of LIT_END ops of higher depth than the literal matcher that
     * encountered them, shared by all literal matchers of this clause
     * matcher. See @b OptimizedLiteralMatcher. */
    Stack<CodeOp*> futureCache;

    static const unsigned sresNoLiteral=static_cast<unsigned>(-1);
    unsigned sresLiteral;

    /**
     * Literal infos that we will attempt to match
     * For each equality we add two lit infos, once with reversed arguments.
     * The order of infos is:
     *
     * Ground literals
     * Non-ground literals
     * [if sres] Ground literals negated
     * [if sres] Non-ground literals negated
     */
    DArray<LitInfo> lInfos;

    Stack<Recycled<OptimizedLiteralMatcher, NoReset>> lms;
  };

protected:
  /** Minimal weight of shared prefix ops (see mergeOpWeight) between
   * consecutive literals for the next literal's code to be merged behind the
   * previous literal end as an alternative branch (and for reordering
   * literals to enable such merges). */
  static const unsigned nextLitAlternativeThreshold = 1;

  void optimizeLiteralOrder(DArray<Literal*>& lits);
  size_t evalSharingBetweenLiterals(Literal* lit1, Literal* lit2);
};


};

#endif // __ClauseCodeTree__
