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

private:

  //////// member variables //////////

#if VDEBUG
  unsigned _clauseMatcherCounter;
#endif

};

template<bool higherOrder>
class OptimizedClauseCodeTree : public ClauseCodeTree<higherOrder> {
  using Base = ClauseCodeTree<higherOrder>;

  // Types inherited from ClauseCodeTree / CodeTree. Because the base class is
  // dependent on the template parameter, these names are not visible to
  // unqualified lookup, so we re-introduce them here.
  using CodeOp = typename Base::CodeOp;
  using ILStruct = typename Base::ILStruct;
  using CodeStack = typename Base::CodeStack;
  using LitCompiler = typename Base::LitCompiler;
  using SearchStruct = typename Base::SearchStruct ;
  using LitInfo = typename Base::LitInfo;
  using RemovingLiteralMatcher = typename Base::RemovingLiteralMatcher;
  using CodeBlock = typename Base::CodeBlock;
  using InitialLiteralOrderingComparator = typename Base::InitialLiteralOrderingComparator;

  // Member functions / variables from the dependent base classes.
  using Base::optimizeLiteralOrder;
  using Base::incorporate;
  using Base::getEntryPoint;
  using Base::compressCheckOps;
  using Base::incTimeStamp;
  using Base::removeOneOfAlternatives;
  using Base::buildBlock;
  using Base::_curTimeStamp;
  using Base::isEmpty;
  using Base::_entryPoint;

public:
  void insert(Clause* cl);
  void remove(Clause* cl);

private:

  // Compilation
  class Incorporator {
  public:
    struct EvalSharingEntry {
      CodeOp* entry;
      unsigned distance;

      // CodeBlock Information
      CodeBlock* block;
      CodeOp** reference;
      ILStruct* ils;
    };
    Incorporator(Clause* cl, OptimizedClauseCodeTree& t);
    void incorporate();

    struct MatchedBlock {
      bool partial;
      unsigned litIndex; // index into lits
      unsigned matchedOps;
      Stack<CodeOp*> matchedPath;
      
      // CodeBlock information
      CodeBlock* block; // this can be before the actual entry
      CodeOp** reference; // reference pointing to this block
      ILStruct* ils;

      CodeOp** addNextOp(unsigned sharedPrefix, OptimizedClauseCodeTree& tree, bool matchedFull);
      void appendBlock(CodeBlock* nextBlock);
      CodeOp** findInsertionReference();
    };

    const unsigned NextOpThreshold = 1;

  private:
    void swap(unsigned i1, unsigned i2);
    void optimizeLiteralOrder();
    void optimizeInterClausalLiteralOrder();
    void optimizeIntraClausalLiteralOrder();
    unsigned evalSharingBetweenLits(const CodeStack &l1code, const CodeStack &l2code);
    MatchedBlock evalSharing(unsigned litIndex, const Stack<EvalSharingEntry>& startOps, Stack<EvalSharingEntry>& nextEntries);
    CodeOp* buildBlock(unsigned litIndex, unsigned matchedCnt, ILStruct* prev, bool stop, bool addedNextOp);
    void destroy();

    MatchedBlock fullyMatchedBlock, partiallyMatchedBlock;
    bool fullyMatched = false;
    bool partiallyMatched = false;
    Clause *clause;
    DArray<Literal*> lits;
    DArray<unsigned> sharedPrefixes;
    DArray<CodeStack> codes;
    OptimizedClauseCodeTree &tree;
    unsigned clen = 0;
  };

  friend Incorporator;
  unsigned nextCnt = 0;
};


};

#endif // __ClauseCodeTree__
