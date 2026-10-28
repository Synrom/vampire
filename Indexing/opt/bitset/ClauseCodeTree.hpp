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
 * @file BitsetClauseCodeTree.hpp
 * Defines class BitsetClauseCodeTree.
 */

#ifndef __BitsetClauseCodeTree__
#define __BitsetClauseCodeTree__

#include "Forwards.hpp"

#include "Lib/Allocator.hpp"
#include "Lib/DArray.hpp"
#include "Lib/Stack.hpp"

#include "CodeTree.hpp"


namespace Indexing {

using namespace Lib;
using namespace Kernel;

class BitsetClauseCodeTree : public BitsetCodeTree
{
protected:
  void onCodeOpDestroying(CodeOp* op) override;
  void printSuccess(std::ostream& out, const CodeOp& op) const override;

public:
  BitsetClauseCodeTree();

  void insert(Clause* cl);
  void remove(Clause* cl);
  Continuation rootContinuation;

private:

  //////// insertion //////////

  struct InitialLiteralOrderingComparator;

  void optimizeLiteralOrder(DArray<Literal*>& lits);
  void evalSharing(CodeStack& code, CodeOp* startOp, size_t& sharedLen, size_t& unsharedLen, CodeOp*& nextOp);
  static void matchCode(CodeStack& code, CodeOp* startOp, size_t& matchedCnt, CodeOp*& nextOp);

  //////// removal //////////

  bool removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks);

  struct RemovingLiteralMatcher
  : public Matcher</*removing*/true,false>
  {
    void init(CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_,
	    const BitsetClauseCodeTree& tree_, Stack<CodeOp*>* firstsInBlocks_);

    USE_ALLOCATOR(RemovingLiteralMatcher);
  };

  //////// retrieval //////////

  /** Context for finding matches of literals
   *
   * Here the actual execution of the code of the tree takes place */
  struct LiteralMatcher
  : public Matcher</*removing*/false,false>
  {
    void init(const BitsetCodeTree& tree, CodeOp* entry_, LitInfo* linfos_, size_t linfoCnt_, bool seekOnlySuccess=false, uint64_t normal=UINT64_MAX, uint64_t opposite=UINT64_MAX);
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
    void init(BitsetClauseCodeTree* tree_, Clause* query_, bool sres_);
    void reset();
    bool keepRecycled() const { return lInfos.keepRecycled(); }

    uint64_t executedCodeOps = 0;
    Clause* next(int& resolvedQueryLit);

    bool matched() { return lms.isNonEmpty() && lms.top()->success(); }
    CodeOp* getSuccessOp() { ASS(matched()); return lms.top()->op; }

    USE_ALLOCATOR(ClauseMatcher);

  private:
    struct MaskPair { uint64_t normal, opposite; };
    struct OrderRow { uint64_t below[2], above[2]; bool ready; };
    struct DomainCache {
      MaskPair masks, below, above;
      bool ready=false, belowReady=false, aboveReady=false;
    };
    DomainCache cachedDomains[64];
    unsigned headerIds[128], headerCount;
    MaskPair headerMasks[128];
    MaskPair headerMask(unsigned header);
    MaskPair support(unsigned depth, bool greater, MaskPair d);
    OrderRow orderRows[128];
    Literal* signedLiterals[128];
    bool smallQuery;
    uint64_t fullMask;
    MaskPair filter(const Continuation& c);
    const OrderRow& orderRow(unsigned signedIndex);
    MaskPair domain(unsigned depth);
    void enterLiteral(CodeOp* entry, bool seekOnlySuccess);
    void leaveLiteral();
    bool canEnterLiteral(CodeOp* op);

    bool checkCandidate(Clause* cl, int& resolvedQueryLit);
    bool matchGlobalVars(int& resolvedQueryLit);
    bool compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq);

    bool existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* oi);

    Clause* query;
    BitsetClauseCodeTree* tree;
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


};

#endif // __BitsetClauseCodeTree__
