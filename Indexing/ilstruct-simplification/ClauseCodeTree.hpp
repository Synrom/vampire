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

#ifndef __ClauseCodeTree_Ablation_ILStructSimplification__
#define __ClauseCodeTree_Ablation_ILStructSimplification__

#include "Forwards.hpp"

#include "Lib/Allocator.hpp"
#include "Lib/DArray.hpp"
#include "Lib/Stack.hpp"

#include "CodeTree.hpp"


namespace Indexing {
namespace Ablation {
namespace ILStructSimplification {

using namespace Lib;
using namespace Kernel;

class ClauseCodeTree : public CodeTree
{
protected:
  void printSuccess(std::ostream& out, const CodeOp& op) const override;

public:
  ClauseCodeTree();

  void insert(Clause* cl);
  void remove(Clause* cl);

private:

  //////// insertion //////////

  struct InitialLiteralOrderingComparator;

  void optimizeLiteralOrder(DArray<Literal*>& lits);
  void incorporate(CodeStack& code);

  static unsigned sharedPrefix(const CodeOp* first, const CodeOp* second);
  // A tree position used to resume literal matching or record its result.
  struct InsertionPosition {
    CodeOp* op = nullptr;
    CodeBlock* block = nullptr;
    // The incoming link to the block, updated if incorporation replaces it.
    CodeOp** blockReference = nullptr;
    // Number of compiled literal operations already matched, including any
    // shared prefix skipped on entry. A complete match includes LIT_END;
    // in that case op points to LIT_END rather than the following operation.
    unsigned matchedPrefixLength = 0;
  };

  InsertionPosition matchCode(const CodeOp* code, unsigned length,
      const Stack<InsertionPosition>& entries, unsigned shared,
      Stack<InsertionPosition>& nextEntries, bool compress);

  //////// removal //////////

  bool removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks);

  using Continuation = ILStruct::Continuation;

  struct RemovingLiteralMatcher
  : public Matcher</*removing*/true,false>
  {
    void init(CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_,
	ClauseCodeTree* tree_, Stack<CodeOp*>* firstsInBlocks_, unsigned slotCount=0,
        const Stack<ILStruct::Continuation>* continuations_=nullptr,
        const Matcher</*removing*/true,false>* source_=nullptr);

    void doEagerMatching();
    bool next();

    bool _eagerlyMatched = false;
    Stack<CodeOp*> eagerResults;
    Stack<Stack<CodeOp*>> eagerResultsFirstInBlocks;

    USE_ALLOCATOR(RemovingLiteralMatcher);
  };

  //////// retrieval //////////

  /** Context for finding matches of literals
   *
   * Here the actual execution of the code of the tree takes place */
  struct LiteralMatcher
  : public Matcher</*removing*/false,false>
  {
    void init(const CodeTree& tree_, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, bool seekOnlySuccess,
        const Stack<ILStruct::Continuation>* continuations_=nullptr,
        const Matcher</*removing*/false,false>* source_=nullptr,
        unsigned slotCount=0);
    bool next();
    bool doEagerMatching();

    inline bool eagerlyMatched() const { return _eagerlyMatched; }

    inline ILStruct* getILS() { ASS(matched()); return op->getILS(); }

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
    void enterLiteral(CodeOp* entry, bool seekOnlySuccess, ILStruct* ils);
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

private:

  //////// member variables //////////

#if VDEBUG
  unsigned _clauseMatcherCounter;
#endif

};

}
}

};

#endif // __ClauseCodeTree_Ablation_ILStructSimplification__
