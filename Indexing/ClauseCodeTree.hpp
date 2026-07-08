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
  void evalSharing(Literal* lit, CodeOp* startOp, size_t& sharedLen, size_t& unsharedLen, CodeOp*& nextOp, size_t& length);
  static void matchCode(CodeStack& code, CodeOp* startOp, size_t& matchedCnt, CodeOp*& nextOp);

  //////// removal //////////

  bool removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks);

  struct RemovingLiteralMatcher
  : public Matcher</*removing*/true,false,higherOrder>
  {
    using Base = Matcher</*removing*/true,false,higherOrder>;

    void init(CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_,
	ClauseCodeTree* tree_, Stack<CodeOp*>* firstsInBlocks_);

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

    void init(CodeTree* tree, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, bool seekOnlySuccess=false);
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

    unsigned countCanEnterLiteral=0;
    unsigned countCheckCandidate=0;
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

  void insert(Clause* cl);
  void remove(Clause* cl);
  void incorporate(CodeTree::CodeStack& code, ILStruct** matchedIls);
  bool canMergeLiterals(CodeStack& code, unsigned startA, unsigned startB);

protected:
  void checkILStructEnumeration();

  /** Literal matcher that merges one single-literal matcher per LitInfo. */
  struct OptimizedLiteralMatcher
  : public CodeTree::Matcher</*removing*/false,false,higherOrder>
  {
    using SLMatcher = CodeTree::SingleLiteralMatcher<higherOrder>;
    using Base = CodeTree::Matcher</*removing*/false,false,higherOrder>;
    using Base::op;
    using Base::_matched;
    using Base::execute;
    using Base::entry;
    using Base::fresh;

    void init(OptimizedClauseCodeTree* tree, Clause* query_, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, bool seekOnlySuccess=false);
    bool next();
    bool doEagerMatching() { ASSERTION_VIOLATION; return false; }

    inline bool eagerlyMatched() const { return true; }
    inline bool matched() const { return _matched && op->isLitEnd(); }
    inline bool success() const { return _matched && op->isSuccess(); }
    inline bool finished() const { return minRank==FINISHED_RANK; }

    inline ILStruct* getILS() { ASS(matched()); return op->getILS(); }

    USE_ALLOCATOR(OptimizedLiteralMatcher);

  private:
    void recordMatch(SLMatcher& m);
    bool shouldIgnore(CodeOp* op) const;

    OptimizedClauseCodeTree* tree;
    Clause* query;
    size_t cnt;
    unsigned minRank;
    /**
     * State of a SingleLiteralMatcher in the merge:
     * - 0 if it must be advanced to its next LIT_END/SUCCESS,
     * - splitNumber+1 if it is stopped at a not-yet-returned LIT_END,
     * - FINISHED_RANK if it has exhausted all alternatives.
     */
    static const unsigned FINISHED_RANK=static_cast<unsigned>(-1);
    DArray<unsigned> ranks;
    Stack<CodeOp*> successes;
    Stack<SLMatcher> matchers;
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

    unsigned countCanEnterLiteral=0;
    unsigned countCheckCandidate=0;
    USE_ALLOCATOR(OptimizedClauseMatcher);

  private:
    void enterLiteral(CodeOp* entry, bool seekOnlySuccess);
    void leaveLiteral();
    bool canEnterLiteral(CodeOp* op);

    bool checkCandidate(Clause* cl, int& resolvedQueryLit);
    bool matchGlobalVars(int& resolvedQueryLit);
    bool compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq);

    bool existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* oi);

    Clause* query;
    OptimizedClauseCodeTree* tree;
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

  struct OptimizedRemovingLiteralMatcher
  : public CodeTree::Matcher</*removing*/true,false,higherOrder>
  {
    using MatcherBase = CodeTree::Matcher</*removing*/true,false,higherOrder>;
    using MatcherBase::entry;

    void init(CodeTree::CodeOp* entry_, CodeTree::CodeOp* lastEntry, CodeTree::LitInfo* linfos_, size_t linfoCnt_,
      OptimizedClauseCodeTree* tree_, Stack<CodeTree::CodeOp*>* firstsInBlocks_);
    bool execute();

    USE_ALLOCATOR(OptimizedRemovingLiteralMatcher);

  private:
    CodeTree::CodeOp* secondEntry;
  };
};


};

#endif // __ClauseCodeTree__
