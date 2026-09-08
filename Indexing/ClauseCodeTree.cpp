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
 * @file ClauseCodeTree.cpp
 * Implements class ClauseCodeTree.
 */

#include <utility>

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Comparison.hpp"
#include "Lib/Int.hpp"
#include "Lib/Recycled.hpp"
#include "Lib/TriangularArray.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Term.hpp"

#include "ClauseCodeTree.hpp"

#undef RSTAT_COLLECTION
#define RSTAT_COLLECTION 0

namespace Indexing
{

using namespace std;
using namespace Lib;
using namespace Kernel;

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::onCodeOpDestroying(CodeOp* op)
{
  if (op->isLitEnd()) {
    delete op->getILS(); 
  }
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::printSuccess(std::ostream& out, const CodeOp& op) const
{
  out << op.getSuccessResult<Clause>()->toString();
}

template<bool higherOrder>
ClauseCodeTree<higherOrder>::ClauseCodeTree()
{
  _clauseCodeTree=true;
#if VDEBUG
  _clauseMatcherCounter=0;
#endif
}

//////////////// insertion ////////////////////

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::insert(Clause* cl)
{
  unsigned clen=cl->length();
  static DArray<Literal*> lits;
  lits.initFromArray(clen, *cl);

  optimizeLiteralOrder(lits);

  CodeStack code;
  LitCompiler compiler(code);

  for(unsigned i=0;i<clen;i++) {
    compiler.nextLit();
    compiler.handleTerm(lits[i]);
    ASS(code.top().isLitEnd());
    code.top().getILS()->depth = i;
  }
  code.push(CodeOp::getSuccess(cl));

  compiler.updateCodeTree(this);

  incorporate(code);
  ASS(code.isEmpty());
}

template<bool higherOrder>
struct ClauseCodeTree<higherOrder>::InitialLiteralOrderingComparator
{
  Comparison compare(Literal* l1, Literal* l2)
  {
    if(l1->weight()!=l2->weight()) {
      return Int::compare(l2->weight(), l1->weight());
    }
    return Int::compare(l1->getId(), l2->getId());
  }
};

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::optimizeLiteralOrder(DArray<Literal*>& lits)
{
  unsigned clen=lits.size();
  if(isEmpty() || clen<=1) {
    return;
  }

  lits.sort(InitialLiteralOrderingComparator());


  CodeOp* entry=getEntryPoint();
  for(unsigned startIndex=0;startIndex<clen-1;startIndex++) {
//  for(unsigned startIndex=0;startIndex<1;startIndex++) {

    size_t unshared=1;
    unsigned bestIndex=startIndex;
    size_t bestSharedLen;
    bool bestGround=lits[startIndex]->ground();
    CodeOp* nextOp;
    evalSharing(lits[startIndex], entry, bestSharedLen, unshared, nextOp);
    if(!unshared) {
      goto have_best;
    }

    for(unsigned i=startIndex+1;i<clen;i++) {
      size_t sharedLen;
      evalSharing(lits[i], entry, sharedLen, unshared, nextOp);
      if(!unshared) {
	bestIndex=i;
        goto have_best;
      }

      if(sharedLen>bestSharedLen && (!bestGround || lits[i]->ground()) ) {
//	cout<<lits[i]->toString()<<" is better than "<<lits[bestIndex]->toString()<<endl;
	bestSharedLen=sharedLen;
	bestIndex=i;
	bestGround=lits[i]->ground();
      }
    }

  have_best:
    swap(lits[startIndex],lits[bestIndex]);

    if(unshared) {
      //we haven't matched the whole literal, so we won't proceed with the next one
      return;
    }
    ASS(nextOp);
    entry=nextOp;
  }
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::evalSharing(Literal* lit, CodeOp* startOp, size_t& sharedLen, size_t& unsharedLen, CodeOp*& nextOp)
{
  CodeStack code;
  LitCompiler compiler(code);

  compiler.handleTerm(lit);

  matchCode(code, startOp, sharedLen, nextOp);

  unsharedLen=code.size()-sharedLen;

  ASS(code.top().isLitEnd());
  delete code.pop().getILS();
}

/**
 * Match the operations in @b code CodeStack on the code starting at @b startOp.
 *
 * Into @b matchedCnt assign number of matched operations and into @b lastAttemptedOp
 * the last operation on which we have attempted matching. If @b matchedCnt==code.size(),
 * the @b lastAttemptedOp is equal to the last operation in the @b code stack, otherwise
 * it is the first operation on which mismatch occurred and there was no alternative to
 * proceed to (in this case it therefore holds that @b lastAttemptedOp->alternative==0 ).
 */
template<bool higherOrder>
void ClauseCodeTree<higherOrder>::matchCode(CodeStack& code, CodeOp* startOp, size_t& matchedCnt, CodeOp*& nextOp)
{
  size_t clen=code.length();
  CodeOp* treeOp=startOp;

  if (clen == 0) {
    matchedCnt = 0;
    nextOp = startOp;
    return;
  }

  for(size_t i=0;i<clen;i++) {
    for(;;) {
      if(treeOp->isSearchStruct()) {
	SearchStruct* ss=treeOp->getSearchStruct();
	CodeOp** toPtr;
	if(ss->getTargetOpPtr<false>(code[i], toPtr) && *toPtr) {
	  treeOp=*toPtr;
	  continue;
	}
      }
      else if(code[i].equalsForOpMatching(*treeOp)) {
	break;
      }
      ASS_NEQ(treeOp,treeOp->alternative());
      treeOp=treeOp->alternative();
      if (!treeOp) {
        matchedCnt=i;
        nextOp=0;
        return;
      }
    }

    //the SEARCH_STRUCT operation does not occur in a CodeBlock
    ASS(!treeOp->isSearchStruct());
    //we can safely do increase because as long as we match and something
    //remains in the @b code stack, we aren't at the end of the CodeBlock
    //either (as each code block contains at least one FAIL or SUCCESS
    //operation, and CodeStack contains at most one SUCCESS as the last
    //operation)
    if (treeOp->isLitEnd() && !treeOp->getILS()->hasSuccessor) {
        matchedCnt=i;
        nextOp=0;
        return;
    }
    treeOp++;
  }
  treeOp--;
  //we matched the whole CodeStack
  matchedCnt=clen;
  if (treeOp->hasSuccessor()) {
    nextOp=treeOp+1;
  } else {
    nextOp = startOp;
  }
}


//////////////// removal ////////////////////

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::remove(Clause* cl)
{
  static DArray<LitInfo> lInfos;
  Recycled<Stack<CodeOp*>> firstsInBlocks;
  Recycled<Stack<Recycled<RemovingLiteralMatcher, NoReset>>> rlms;

  unsigned clen=cl->length();
  lInfos.ensure(clen);

  if(!clen) {
    CodeOp* op=getEntryPoint();
    firstsInBlocks->push(op);
    if(!removeOneOfAlternatives(op, cl, &*firstsInBlocks)) {
      ASSERTION_VIOLATION;
      INVALID_OPERATION("empty clause to be removed was not found");
    }
    return;
  }

  for(unsigned i=0;i<clen;i++) {
    lInfos[i]=LitInfo(cl,i);
    lInfos[i].liIndex=i;
  }
  incTimeStamp();

  CodeOp* op=getEntryPoint();
  firstsInBlocks->push(op);
  unsigned depth=0;
  for(;;) {
    RemovingLiteralMatcher* rlm = 0;
    {
      Recycled<RemovingLiteralMatcher, NoReset> rrlm; // take rlm out of recycling
      rlm = &*rrlm; // get the actual content (also to use after this initialization block)
      rlm->init(op, lInfos.array(), lInfos.size(), *this, &*firstsInBlocks); // init it
      rlms->push(std::move(rrlm)); // store it in rlms (along with the obligation to return to recycling when no longer used)
    }

  iteration_restart:
    if(!rlm->execute()) {
      if(depth==0) {
        ASSERTION_VIOLATION;
        INVALID_OPERATION("clause to be removed was not found");
      }
      rlms->pop();
      depth--;
      rlm = &*rlms->top();
      goto iteration_restart;
    }

    op=rlm->op;
    ASS(op->isLitEnd());
    ASS_EQ(op->getILS()->depth, depth);

    if(op->getILS()->timestamp==_curTimeStamp) {
      //we have already been here
      goto iteration_restart;
    }
    op->getILS()->timestamp=_curTimeStamp;

    op++;
    if(depth==clen-1) {
      if(removeOneOfAlternatives(op, cl, &*firstsInBlocks)) {
        //successfully removed
        break;
      }
      goto iteration_restart;
    }
    ASS_L(depth,clen-1);
    depth++;
  }

  for(unsigned i=0;i<clen;i++) {
    lInfos[i].dispose();
  }
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::RemovingLiteralMatcher::init(CodeOp* entry_, LitInfo* linfos_,
    size_t linfoCnt_, const ClauseCodeTree& tree_, Stack<CodeOp*>* firstsInBlocks_)
{
  Base::init(tree_, entry_, linfos_, linfoCnt_, firstsInBlocks_);

  ALWAYS(Base::prepareLiteral());
}

/**
 * The first operation of the CodeBlock containing @b op
 * must already be on the @b firstsInBlocks stack.
 */
template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks)
{
  unsigned initDepth=firstsInBlocks->size();

  CodeOp* ilsOp = op-1;
  ASS(ilsOp->isLitEnd());

  while(!op->isSuccess() || op->getSuccessResult<Clause>()!=cl) {
    op=op->alternative();
    if(!op) {
      firstsInBlocks->truncate(initDepth);
      return false;
    }
    firstsInBlocks->push(op);
  }
  ILStruct* ils = ilsOp->getILS();
  while (ils) {
    if (ils->nrChildren) ils->nrChildren--;
    if (ils->nrChildren) break;
    ils = ils->previous;
  }
  op->makeFail();
  optimizeMemoryAfterRemoval(firstsInBlocks, op);
  return true;
}

//////////////// optimized clause code tree ////////////////////

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::optimizeLiteralOrder(DArray<Literal*>& lits)
{
  unsigned clen=lits.size();
  int unsharedIndex = -1;
  CodeTree::CodeOp* entry=0;
  if(this->isEmpty() || clen<=1) {
    unsharedIndex = 0;
    goto optimize_intra_clausal_order;
  }

  lits.sort(typename Base::InitialLiteralOrderingComparator());

  entry=this->getEntryPoint();
  for(unsigned startIndex=0;startIndex<clen-1;startIndex++) {
//  for(unsigned startIndex=0;startIndex<1;startIndex++) {

    size_t unshared=1;
    unsigned bestIndex=startIndex;
    size_t bestSharedLen;
    bool bestGround=lits[startIndex]->ground();
    CodeTree::CodeOp* nextOp;
    this->evalSharing(lits[startIndex], entry, bestSharedLen, unshared, nextOp);
    if(!unshared) {
      goto have_best;
    }

    for(unsigned i=startIndex+1;i<clen;i++) {
      size_t sharedLen;
      this->evalSharing(lits[i], entry, sharedLen, unshared, nextOp);
      if(!unshared) {
	      bestIndex=i;
        goto have_best;
      }
      if(sharedLen>bestSharedLen &&  (!bestGround || lits[i]->ground())) {
//	cout<<lits[i]->toString()<<" is better than "<<lits[bestIndex]->toString()<<endl;
	bestSharedLen=sharedLen;
	bestIndex=i;
	bestGround=lits[i]->ground();
      }
    }

  have_best:
    swap(lits[startIndex],lits[bestIndex]);

    if(unshared) {
      //we haven't matched the whole literal, so we won't proceed with the next one
      unsharedIndex = startIndex;
      break;
    }
    ASS(nextOp);
    entry=nextOp;
  }
  
optimize_intra_clausal_order:
  if (unsharedIndex >= clen - 1) return;

  for (unsigned startIndex=unsharedIndex; startIndex < clen-2; startIndex++) {
    unsigned bestIndex = startIndex+1;
    size_t bestSharedLen = evalSharingBetweenLiterals(lits[startIndex], lits[bestIndex]);
    for (unsigned i=startIndex+2; i < clen; i++) {
      size_t sharedLen = evalSharingBetweenLiterals(lits[startIndex], lits[i]);
      if (sharedLen > bestSharedLen) {
        bestSharedLen = sharedLen;
        bestIndex = i;
      }
    }
    //reordering pays off only if it enables a merge in insert(); otherwise it
    //overrides the tree-sharing order from above and slows down matching
    if (bestSharedLen > nextLitAlternativeThreshold) {
      swap(lits[startIndex+1], lits[bestIndex]);
    }
  }
}

/**
 * Weight of a CodeOp for the merge decision in OptimizedClauseCodeTree::insert.
 * Merging saves re-executing the shared prefix in the next literal's matcher,
 * and ops differ in how much execution they save: variable ops (which also
 * match anything, so they are executed on every query literal) and ground term
 * comparisons are more costly than a simple functor check.
 */
static unsigned mergeOpWeight(const CodeTree::CodeOp& op)
{
  switch (op._instruction()) {
    case CodeTree::ASSIGN_VAR:
    case CodeTree::CHECK_GROUND_TERM:
      return 3;
    case CodeTree::CHECK_VAR:
      return 2;
    default:
      return 1;
  }
}

template<bool higherOrder>
size_t OptimizedClauseCodeTree<higherOrder>::evalSharingBetweenLiterals(Literal* lit1, Literal* lit2)
{
  CodeTree::CodeStack lit1Code;
  CodeTree::LitCompiler lit1Compiler(lit1Code);
  lit1Compiler.handleTerm(lit1);

  CodeTree::CodeStack lit2Code;
  CodeTree::LitCompiler lit2Compiler(lit2Code);
  lit2Compiler.handleTerm(lit2);

  size_t shared = 0;
  for(size_t i=0; i < lit1Code.length() && i < lit2Code.length(); i++) {
    if(!lit1Code[i].equalsForOpMatching(lit2Code[i])) {
      break;
    }
    if(lit1Code[i].isLitEnd()) {
      break;
    }
    shared += mergeOpWeight(lit1Code[i]);
  }

  while(lit1Code.isNonEmpty()) {
    if(lit1Code.top().isLitEnd()) {
      delete lit1Code.top().getILS();
    }
    lit1Code.pop();
  }
  while(lit2Code.isNonEmpty()) {
    if(lit2Code.top().isLitEnd()) {
      delete lit2Code.top().getILS();
    }
    lit2Code.pop();
  }

  return shared;
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::insert(Clause* cl)
{
  unsigned clen=cl->length();
  static DArray<Literal*> lits;
  lits.initFromArray(clen, *cl);

  optimizeLiteralOrder(lits);

  CodeTree::CodeStack code;
  CodeTree::LitCompiler compiler(code);

  unsigned lastLitOffset = 0;
  bool lastLiteralWasMerged = false;
  for(unsigned i=0;i<clen;i++) {
    unsigned loffset = lastLitOffset;
    unsigned roffset = code.length();

    compiler.nextLit();
    compiler.handleTerm(lits[i]);
    ASS(code.top().isLitEnd());
    code.top().getILS()->depth = i;

    unsigned newLitInstr = code.length() - roffset;
    unsigned oldLitInstr = roffset - loffset;

    unsigned cmpLength = min(newLitInstr, oldLitInstr);
    unsigned matchedWeight = 0;
    for (unsigned idx=0; idx < cmpLength; idx++) {
      if (code[loffset + idx].equalsForOpMatching(code[roffset + idx])) {
        matchedWeight += mergeOpWeight(code[loffset + idx]);
      } else {
        break;
      }
    }

    if (matchedWeight > nextLitAlternativeThreshold && !lastLiteralWasMerged) {
      lastLiteralWasMerged = true;
      ILStruct* prev;
      CodeTree::CodeStack insertedCode(roffset);
      for (unsigned k=0; k < roffset; k++) {
        if (code[k].isLitEnd()) {
          prev = new ILStruct(*code[k].getILS());
          insertedCode.push(CodeTree::CodeOp::getLitEnd(prev));
        } else {
          insertedCode.push(code[k]);
        }
      }
      incorporate(insertedCode, &prev);
      ASS(prev);
      for (unsigned k=0; k < oldLitInstr; k++) {
        if (code[loffset + k].isLitEnd()) {
          delete code[loffset+k].getILS();
        }
      }
      for (unsigned k=0; k < newLitInstr; k++) {
        swap(code[loffset+k], code[roffset+k]);
        if (code[loffset+k].isLitEnd()) {
          code[loffset + k].getILS()->putIntoSequence(prev);
        }
      }
      code.truncate(code.length() - oldLitInstr);
    } else {
      lastLitOffset = roffset;
      lastLiteralWasMerged = false;
    }
  }
  code.push(CodeTree::CodeOp::getSuccess(cl));

  compiler.updateCodeTree(this);

  incorporate(code, nullptr);
  ASS(code.isEmpty());
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::remove(Clause* cl)
{
  Stack<CodeOp*> queue; 
  queue.push(getEntryPoint());

  bool removed = false;
  while (queue.isNonEmpty()) {
    CodeOp* bOp = queue.pop();
    if (bOp->isSearchStruct()) {
      auto ss = bOp->getSearchStruct();
      for (unsigned i=0; i < ss->length(); i++) {
        if (ss->targets[i]) {
          queue.push(ss->targets[i]);
        }
      }
    } else {
      CodeBlock* block = CodeTree::firstOpToCodeBlock(bOp);
      for (unsigned i=0; i < block->length(); i++) {
        CodeOp* op = &(*block)[i];
        if (op->alternative()) {
          queue.push(op->alternative());
        }
        if (op->isSuccess() && op->getSuccessResult<Clause>() == cl) {
          op->makeFail();
          queue.reset();
          removed = true;
        }
      }
    }
  }
  ASS(removed);
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::incorporate(CodeTree::CodeStack& code, CodeTree::ILStruct** prev)
{

  if(this->isEmpty()) {
    this->_entryPoint=CodeTree::buildBlock(code, code.length(), 0);
    code.reset();
    return;
  }

  static const unsigned checkFunOpThreshold=5; //must be greater than 1 or it would cause loops
  static const unsigned checkGroundTermOpThreshold=3; //must be greater than 1 or it would cause loops

  size_t clen=code.length();
  CodeTree::CodeOp** tailTarget;
  size_t matchedCnt;
  CodeTree::ILStruct* lastMatchedILS=0;
  CodeTree::CodeOp** currentBlockReference = nullptr;
  bool appendToBlock = false;

  {
    CodeTree::CodeOp* treeOp = this->getEntryPoint();

    for (size_t i = 0; i < clen; i++) {
      CodeTree::CodeOp* chainStart = treeOp;
      CodeTree::CodeOp** chainStartReference = currentBlockReference;
      size_t checkFunOps = 0;
      size_t checkGroundTermOps = 0;
      for (;;) {
        if (treeOp->isSearchStruct()) {
          //handle the SEARCH_STRUCT
          CodeTree::SearchStruct* ss = treeOp->getSearchStruct();
          CodeTree::CodeOp** toPtr;
          if (ss->getTargetOpPtr<true>(code[i], toPtr)) {
            if (!*toPtr) {
              tailTarget = toPtr;
              matchedCnt = i;
              goto matching_done;
            }
            currentBlockReference = toPtr;
            treeOp = *toPtr;
            continue;
          }
        } else if (code[i].equalsForOpMatching(*treeOp)) {
          //matched, go to the next compiled instruction
          break;
        }

        if (treeOp->alternative()) {
          //try alternative if there is some
          currentBlockReference = &treeOp->alternative();
          treeOp = treeOp->alternative();
        } else {
          //matching failed, we'll add the new branch here
          tailTarget = &treeOp->alternative();
          matchedCnt = i;
          goto matching_done;
        }

        if (treeOp->isCheckFun()) {
          checkFunOps++;
          //if there were too many CHECK_FUN alternative operations, put them
          //into a SEARCH_STRUCT
          if (checkFunOps > checkFunOpThreshold) {
            //we put CHECK_FUN ops into the SEARCH_STRUCT op, and
            //restart with the chain
            this->template compressCheckOps<CodeTree::SearchStruct::FN_STRUCT>(chainStart);
            treeOp = chainStart;
            checkFunOps = 0;
            checkGroundTermOps = 0;
            currentBlockReference = chainStartReference;
            continue;
          }
        }

        if (treeOp->isCheckGroundTerm()) {
          checkGroundTermOps++;
          //if there were too many CHECK_GROUND_TERM alternative operations, put them
          //into a SEARCH_STRUCT
          if (checkGroundTermOps > checkGroundTermOpThreshold) {
            //we put CHECK_GROUND_TERM ops into the SEARCH_STRUCT op, and
            //restart with the chain
            this->template compressCheckOps<CodeTree::SearchStruct::GROUND_TERM_STRUCT>(chainStart);
            treeOp = chainStart;
            checkFunOps = 0;
            checkGroundTermOps = 0;
            currentBlockReference = chainStartReference;
            continue;
          }
        }
      } // for(;;)

      if (treeOp->isLitEnd()) {
        lastMatchedILS = treeOp->getILS();
        if (!treeOp->getILS()->hasSuccessor) {
          appendToBlock = true;
          matchedCnt = i+1;
          goto matching_done;
        }
      }

      //the SEARCH_STRUCT operation does not occur in a CodeBlock
      ASS(!treeOp->isSearchStruct());
      ASS(!treeOp->isLitEnd() || treeOp->getILS()->hasSuccessor);
      //we can safely do increase because as long as we match and something
      //remains in the @b code stack, we aren't at the end of the CodeBlock
      //either (as each code block contains at least one FAIL or SUCCESS
      //operation, and CodeStack contains at most one SUCCESS as the last
      //operation)
      if (!treeOp->isSuccess()) {
        treeOp++;
      }
    }
    //We matched the whole CodeStack. If we are here, we are inserting an
    //item multiple times. We will insert it anyway, because later we may
    //be removing it multiple times as well.
    matchedCnt = code[clen-1].isSuccess() ? clen - 1 : clen;

    //we need to find where to put it
    while (treeOp->alternative()) {
      treeOp = treeOp->alternative();
    }
    tailTarget = &treeOp->alternative();
  }
matching_done:

  ASS_LE(matchedCnt, clen);
  RSTAT_MCTR_INC("alt split literal", lastMatchedILS ? (lastMatchedILS->depth+1) : 0);

  CodeTree::CodeBlock* rem = nullptr;
  if (matchedCnt != clen) {
    if (appendToBlock) {
      CodeTree::CodeBlock* oldBlock = currentBlockReference ? CodeTree::firstOpToCodeBlock(*currentBlockReference) : this->_entryPoint;
      rem = CodeTree::appendBlock(code, clen-matchedCnt, oldBlock);
      if (currentBlockReference) {
        *currentBlockReference = &(*rem)[0];
      } else {
        this->_entryPoint = rem;
      }
    } else {
      rem=CodeTree::buildBlock(code, clen-matchedCnt, lastMatchedILS);
      *tailTarget=&(*rem)[0];
      LOG_OP(rem->toString()<<" incorporated, mismatch caused by "<<code[matchedCnt].toString());
    }
  }

  bool setIls = false;
  if (prev)  {
    if (rem) {
      for (unsigned i=rem->length()-1; rem->length() > 0; i--) {
        if ((*rem)[i].isLitEnd()) {
          *prev = (*rem)[i].getILS();
          setIls = true;
          break;
        }
        if (i==0) break;
      }
    }

    if (!setIls) {
      *prev = lastMatchedILS;
      setIls = true;
    }
  }

  //truncate the part that was used and thus does not need disposing
  code.truncate(matchedCnt);
  //dispose of the unused code
  while(code.isNonEmpty()) {
    if(code.top().isLitEnd()) {
      delete code.top().getILS();
    }
    code.pop();
  }
}

////////// LiteralMatcher

/**
 * If @b seekOnlySuccess if true, we will look only for immediate SUCCESS operations
 *  and fail if there isn't any at the beginning (possibly also among alternatives).
 */
template<bool higherOrder>
void ClauseCodeTree<higherOrder>::LiteralMatcher::init(const CodeTree& tree_, CodeOp* entry_,
					  LitInfo* linfos_, size_t linfoCnt_,
					  bool seekOnlySuccess)
{
  ASS_G(linfoCnt_,0);

  Base::init(tree_,entry_,linfos_,linfoCnt_);

  _eagerlyMatched=false;
  eagerResults.reset();

  RSTAT_CTR_INC("LiteralMatcher::init");
  if(seekOnlySuccess) {
    RSTAT_CTR_INC("LiteralMatcher::init - seekOnlySuccess");
    //we are interested only in SUCCESS operations
    //(and those must be at the entry point or its alternatives)

    _eagerlyMatched=true;
    Base::fresh=false;
    CodeOp* sop=Base::entry;
    while(sop) {
      if(sop->isSuccess()) {
        eagerResults.push(sop);
      }
      sop=sop->alternative();
    }
    return;
  }

  ALWAYS(Base::prepareLiteral());
}

/**
 * Try to find a match, and if one is found, return true
 */
template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::LiteralMatcher::next()
{
  if(eagerlyMatched()) {
    _matched=!eagerResults.isEmpty();
    if(!_matched) {
      return false;
    }
    op=eagerResults.pop();
    return true;
  }

  if(finished()) {
    //all possible matches are exhausted
    return false;
  }

  _matched=execute();
  if(!_matched) {
    return false;
  }

  ASS(op->isLitEnd() || op->isSuccess());
  if(op->isLitEnd()) {
    recordMatch();
  }
  return true;
}

/**
 * Perform eager matching and return true iff new matches were found
 */
template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::LiteralMatcher::doEagerMatching()
{
  ASS(!eagerlyMatched()); //eager matching can be done only once
  ASS(eagerResults.isEmpty());
  ASS(!finished());

  //backup the current op
  CodeOp* currOp=op;

  static Stack<CodeOp*> eagerResultsRevOrder;
  static Stack<CodeOp*> successes;
  eagerResultsRevOrder.reset();
  successes.reset();

  while(execute()) {
    if(op->isLitEnd()) {
      recordMatch();
      eagerResultsRevOrder.push(op);
    }
    else {
      ASS(op->isSuccess());
      successes.push(op);
    }
  }

  //we want to yield results in the order we found them
  //(otherwise the subsumption resolution would be preferred to the
  //subsumption)
  while(eagerResultsRevOrder.isNonEmpty()) {
    eagerResults.push(eagerResultsRevOrder.pop());
  }
  //we want to yield SUCCESS operations first (as after them there may
  //be no need for further clause retrieval)
  while(successes.isNonEmpty()) {
    eagerResults.push(successes.pop());
  }

  _eagerlyMatched=true;

  op=currOp; //restore the current op

  return eagerResults.isNonEmpty();
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::LiteralMatcher::recordMatch()
{
  ASS(_matched);

  ILStruct* ils=op->getILS();
  ils->ensureFreshness(Base::tree->_curTimeStamp);
  if(ils->finished) {
    //no need to record matches which we already know will not lead to anything
    return;
  }
  if(!ils->matchCnt && Base::linfos[Base::curLInfo].opposite) {
    //if we're matching opposite matches, we have already tried all non-opposite ones
    ils->noNonOppositeMatches=true;
  }
  ils->addMatch(Base::linfos[Base::curLInfo].liIndex, Base::bindings);
}


////////// OptimizedLiteralMatcher

/**
 * If @b seekOnlySuccess if true, we will look only for immediate SUCCESS operations
 *  and fail if there isn't any at the beginning (possibly also among alternatives).
 */
template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedLiteralMatcher::init(OptimizedClauseCodeTree* tree_, CodeOp* entry_,
					  LitInfo* linfos_, size_t linfoCnt_, unsigned depth_, unsigned lmsIndex_,
					  Stack<CodeOp*>* futureCache_, CodeOp* prevLitEnd_, bool seekOnlySuccess)
{
  ASS_G(linfoCnt_,0);

  Base::init(*tree_,entry_,linfos_,linfoCnt_);

  depth = depth_;
  lmsIndex = lmsIndex_;
  futureCache = futureCache_;
  prevLitEnd = prevLitEnd_;
  _eagerlyMatched=false;
  eagerResults.reset();

  RSTAT_CTR_INC("OptimizedLiteralMatcher::init");
  if(seekOnlySuccess) {
    RSTAT_CTR_INC("OptimizedLiteralMatcher::init - seekOnlySuccess");
    //we are interested only in SUCCESS operations
    //(and those must be at the entry point or its alternatives)

    _eagerlyMatched=true;
    Base::fresh=false;
    CodeOp* sop=Base::entry;
    while(sop) {
      if(sop->isSuccess()) {
        eagerResults.push(sop);
      }
      sop=sop->alternative();
    }
    return;
  }

  ALWAYS(Base::prepareLiteral());
}

/**
 * Remove and return an entry from @b futureCache that was queued for this
 * literal matcher (i.e. one whose ILStruct::previous is the ILStruct of the
 * LIT_END op that led us here), or nullptr if there is none.
 */
template<bool higherOrder>
typename OptimizedClauseCodeTree<higherOrder>::CodeOp*
OptimizedClauseCodeTree<higherOrder>::OptimizedLiteralMatcher::pickFutureCacheEntry()
{
  if(!prevLitEnd) {
    return nullptr;
  }
  for(size_t i=futureCache->size(); i-- > 0;) {
    if((*futureCache)[i]->getILS()->previous == prevLitEnd->getILS()) {
      return futureCache->swapRemove(i);
    }
  }
  return nullptr;
}

/**
 * Try to find a match, and if one is found, return true
 */
template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedLiteralMatcher::next()
{
  if(eagerlyMatched()) {
    _matched=!eagerResults.isEmpty();
    if(!_matched) {
      return false;
    }
    op=eagerResults.pop();
    return true;
  }

  if(CodeOp* cached = pickFutureCacheEntry()) {
    op=cached;
    _matched=true;
    return true;
  }

  if(!Base::entry) {
    //nothing left to try: this literal has no code of its own (its
    //LIT_END has no successor), so all its matches (if any) can only
    //come from futureCache, which we just found to be exhausted
    return false;
  }

  if(finished()) {
    //all possible matches are exhausted
    return false;
  }

  op = Base::entry;
  while ((_matched = execute())) {
    ASS(op->isLitEnd() || op->isSuccess());
    if(op->isLitEnd()) {
      recordMatch();
      if (op->getILS()->depth == depth) {
        return true;
      } else {
        futureCache->push(op);
      }
    } else {
      return true;
    }
  }

  return false;
}

/**
 * Perform eager matching and return true iff new matches were found
 */
template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedLiteralMatcher::doEagerMatching()
{
  ASS(!eagerlyMatched()); //eager matching can be done only once

  //backup the current op
  CodeOp* currOp=op;

  //we want to yield results in the order we found them (otherwise the
  //subsumption resolution would be preferred to the subsumption), so we
  //collect them here in reverse order and transfer them onto eagerResults
  //(a stack) at the end, so that popping eagerResults yields them in the
  //order they were found
  static Stack<CodeOp*> ownResultsRevOrder;
  static Stack<CodeOp*> successes;
  ownResultsRevOrder.reset();
  successes.reset();

  //matches for this literal that were already queued for us by an ancestor
  //literal matcher
  while(CodeOp* cached = pickFutureCacheEntry()) {
    ownResultsRevOrder.push(cached);
  }

  if(Base::entry) {
    op = Base::entry;
    while(execute()) {
      if(op->isLitEnd()) {
        recordMatch();
        if (op->getILS()->depth == depth) {
          ownResultsRevOrder.push(op);
        } else {
          futureCache->push(op);
        }
      }
      else {
        ASS(op->isSuccess());
        successes.push(op);
      }
    }
  }

  while(ownResultsRevOrder.isNonEmpty()) {
    eagerResults.push(ownResultsRevOrder.pop());
  }
  //we want to yield SUCCESS operations first (as after them there may
  //be no need for further clause retrieval)
  while(successes.isNonEmpty()) {
    eagerResults.push(successes.pop());
  }

  _eagerlyMatched=true;

  op=currOp; //restore the current op

  return eagerResults.isNonEmpty();
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedLiteralMatcher::recordMatch()
{
  ASS(_matched);

  ILStruct* ils=op->getILS();
  ils->ensureFreshness(Base::tree->_curTimeStamp);
  if(ils->finished) {
    //no need to record matches which we already know will not lead to anything
    return;
  }
  if(!ils->matchCnt && Base::linfos[Base::curLInfo].opposite) {
    //if we're matching opposite matches, we have already tried all non-opposite ones
    ils->noNonOppositeMatches=true;
  }
  ils->lmsIndex = lmsIndex;
  ils->addMatch(Base::linfos[Base::curLInfo].liIndex, Base::bindings);
}
////////// ClauseMatcher

/**
 * Initialize the ClauseMatcher to retrieve generalizetions
 * of the @b query_ clause.
 * If @b sres_ if true, we perform subsumption resolution
 */
template<bool higherOrder>
void ClauseCodeTree<higherOrder>::ClauseMatcher::init(ClauseCodeTree* tree_, Clause* query_, bool sres_)
{
  ASS(!tree_->isEmpty());

  query=query_;
  tree=tree_;
  sres=sres_;
  lms.reset();

#if VDEBUG
  ASS_EQ(tree->_clauseMatcherCounter,0);
  tree->_clauseMatcherCounter++;
#endif

  //init LitInfo records
  unsigned clen=query->length();
  unsigned baseLICnt=clen;
  for(unsigned i=0;i<clen;i++) {
    if((*query)[i]->isEquality()) {
      baseLICnt++;
    }
  }
  unsigned liCnt=sres ? (baseLICnt*2) : baseLICnt;
  lInfos.ensure(liCnt);

  //we put ground literals first
  unsigned liIndex=0;
  for(unsigned i=0;i<clen;i++) {
    if(!(*query)[i]->ground()) {
      continue;
    }
    lInfos[liIndex]=LitInfo(query,i);
    lInfos[liIndex].liIndex=liIndex;
    liIndex++;
    if((*query)[i]->isEquality()) {
      lInfos[liIndex]=LitInfo::getReversed(lInfos[liIndex-1]);
      lInfos[liIndex].liIndex=liIndex;
      liIndex++;
    }
  }
  for(unsigned i=0;i<clen;i++) {
    if((*query)[i]->ground()) {
      continue;
    }
    lInfos[liIndex]=LitInfo(query,i);
    lInfos[liIndex].liIndex=liIndex;
    liIndex++;
    if((*query)[i]->isEquality()) {
      lInfos[liIndex]=LitInfo::getReversed(lInfos[liIndex-1]);
      lInfos[liIndex].liIndex=liIndex;
      liIndex++;
    }
  }
  if(sres) {
    for(unsigned i=0;i<baseLICnt;i++) {
      unsigned newIndex=i+baseLICnt;
      lInfos[newIndex]=LitInfo::getOpposite(lInfos[i]);
      lInfos[newIndex].liIndex=newIndex;
    }
    sresLiteral=sresNoLiteral;
  }

  tree->incTimeStamp();
  enterLiteral(tree->getEntryPoint(), clen==0);
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::ClauseMatcher::reset()
{
  unsigned liCnt=lInfos.size();
  for(unsigned i=0;i<liCnt;i++) {
    lInfos[i].dispose();
  }
  lms.reset();

#if VDEBUG
  ASS_EQ(tree->_clauseMatcherCounter,1);
  tree->_clauseMatcherCounter--;
#endif
}

/**
 * Return next clause matching query or 0 if there is not such
 */
template<bool higherOrder>
Clause* ClauseCodeTree<higherOrder>::ClauseMatcher::next(int& resolvedQueryLit)
{
  TIME_TRACE("Normal clause matcher next")
  if(lms.isEmpty()) {
    return 0;
  }

  for(;;) {
    LiteralMatcher* lm = &*lms.top();

    //get next literal from the literal matcher
    bool found=lm->next();

    //if there's none, go one level up (or fail if at the top)
    if(!found) {
      leaveLiteral();
      if(lms.isEmpty()) {
	return 0;
      }
    }
    else if(lm->op->isSuccess()) {
      Clause* candidate=lm->op->template getSuccessResult<Clause>();
      RSTAT_MCTR_INC("candidates", lms.size()-1);
      if(checkCandidate(candidate, resolvedQueryLit)) {
	RSTAT_MCTR_INC("candidates (success)", lms.size()-1);
	return candidate;
      }
    }
    else if(canEnterLiteral(lm->op)) {
      ASS(lm->op->isLitEnd());
      ASS_LE(lms.size(), query->length()); //this is due to the seekOnlySuccess part below

      //LIT_END is never the last operation in the CodeBlock,
      //so we can increase here
      CodeOp* newLitEntry=lm->op+1;

      //check that we have cleared the sresLiteral value if it is no longer valid
      ASS(!sres || sresLiteral==sresNoLiteral || sresLiteral<lms.size()-1);

      if(sres && sresLiteral==sresNoLiteral) {
	//we check whether we haven't matched only opposite literals on the previous level
	if(lm->getILS()->noNonOppositeMatches) {
	  sresLiteral=lms.size()-1;
	}
      }

      bool seekOnlySuccess=lms.size()==query->length();
      enterLiteral(newLitEntry, seekOnlySuccess);
    }
  }
}

template<bool higherOrder>
inline bool ClauseCodeTree<higherOrder>::ClauseMatcher::canEnterLiteral(CodeOp* op)
{
  ASS(op->isLitEnd());
  ASS_EQ(lms.top()->op, op);

  ILStruct* ils=op->getILS();
  if(ils->timestamp==tree->_curTimeStamp && ils->visited) {
    return false;
  }

  if(lms.size()>1) {
    //we have already matched and entered some index literals, so we
    //will check for compatibility of variable assignments
    if(ils->varCnt && !lms.top()->eagerlyMatched()) {
      lms.top()->doEagerMatching();
      RSTAT_MST_INC("match count", lms.size()-1, lms.top()->getILS()->matchCnt);
    }
    for(size_t ilIndex=0;ilIndex<lms.size()-1;ilIndex++) {
      ILStruct* prevILS=lms[ilIndex]->getILS();
      if(prevILS->varCnt && !lms[ilIndex]->eagerlyMatched()) {
	lms[ilIndex]->doEagerMatching();
	RSTAT_MST_INC("match count", ilIndex, lms[ilIndex]->getILS()->matchCnt);
      }

      size_t matchIndex=ils->matchCnt;
      while(matchIndex!=0) {
	matchIndex--;
	MatchInfo* mi=ils->getMatch(matchIndex);
	if(!existsCompatibleMatch(ils, mi, prevILS)) {
	  ils->deleteMatch(matchIndex); //decreases ils->matchCnt
	}
      }
      if(!ils->matchCnt) {
	return false;
      }
    }
  }

  return true;
}

/**
 * Enter literal matching starting at @c entry.
 *
 * @param entry the code tree node
 * @param seekOnlySuccess if true, accept only SUCCESS operations
 *   (this is to be used when all literals are matched so we want
 *   to see just clauses that end at this point).
 */
template<bool higherOrder>
void ClauseCodeTree<higherOrder>::ClauseMatcher::enterLiteral(CodeOp* entry, bool seekOnlySuccess)
{
  if(!seekOnlySuccess) {
    RSTAT_MCTR_INC("enterLiteral levels (non-sos)", lms.size());
  }

  if(lms.isNonEmpty()) {
    Recycled<LiteralMatcher, NoReset>& prevLM = lms.top();
    ILStruct* ils=prevLM->op->getILS();
    ASS_EQ(ils->timestamp,tree->_curTimeStamp);
    ASS(!ils->visited);
    ASS(!ils->finished);
    ils->visited=true;
  }

  size_t linfoCnt=lInfos.size();
  if(sres && sresLiteral!=sresNoLiteral) {
    ASS_L(sresLiteral,lms.size());
    //we do not need to match index literals with opposite query
    //literals, as one of already matched index literals matched only
    //to opposite literals (and opposite literals cannot be matched
    //on more than one index literal)
    ASS_EQ(linfoCnt%2,0);
    linfoCnt/=2;
  }

  Recycled<LiteralMatcher, NoReset> lm;
  lm->init(*tree, entry, lInfos.array(), linfoCnt, seekOnlySuccess);
  lms.push(std::move(lm));
}

template<bool higherOrder>
void ClauseCodeTree<higherOrder>::ClauseMatcher::leaveLiteral()
{
  ASS(lms.isNonEmpty());

  lms.pop();

  if(lms.isNonEmpty()) {
    LiteralMatcher* prevLM = &*lms.top();
    ILStruct* ils=prevLM->op->getILS();
    ASS_EQ(ils->timestamp,tree->_curTimeStamp);
    ASS(ils->visited);

    ils->finished=true;

    if(sres) {
      //clear the resolved literal flag if we have backtracked from it
      unsigned depth=lms.size()-1;
      if(sresLiteral==depth) {
        sresLiteral=sresNoLiteral;
      }
      ASS(sresLiteral==sresNoLiteral || sresLiteral<depth);
    }
  }
}


//////////////// Multi-literal matching

template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::ClauseMatcher::checkCandidate(Clause* cl, int& resolvedQueryLit)
{
  unsigned clen=cl->length();
  //the last matcher in mls is the one that yielded the SUCCESS operation
  ASS_EQ(clen, lms.size()-1);
  ASS_EQ(clen, lms[clen-1]->op->getILS()->depth+1);

  if(clen<=1) {
    //if clause doesn't have multiple literals, there is no need
    //for multi-literal matching
    resolvedQueryLit=-1;
    if(sres && clen==1) {
      size_t matchCnt=lms[0]->getILS()->matchCnt;
      for(size_t i=0;i<matchCnt;i++) {
	MatchInfo* mi=lms[0]->getILS()->getMatch(i);
	if(lInfos[mi->liIndex].opposite) {
	  resolvedQueryLit=lInfos[mi->liIndex].litIndex;
	}
	else {
	  //we prefer subsumption to subsumption resolution
	  resolvedQueryLit=-1;
	  break;
	}
      }
    }
    return true;
  }

//  if(matchGlobalVars(resolvedQueryLit)) {
//    return true;
//  }

  bool newMatches=false;
  for(int i=clen-1;i>=0;i--) {
    LiteralMatcher* lm = &*lms[i];
    if(lm->eagerlyMatched()) {
      break;
    }
    if(lm->getILS()->varCnt==0) {
      //If the index term is ground, at most two literals can be matched on
      //it (the second one is the opposite one in case we're performing the
      //subsumption resolution) We are here, so we have matched one already,
      //and we know that the query clause doesn't contain two equal or opposite
      //literals (or else it would have been simplified by duplicate literal
      //removal or by tautology deletion). Therefore we don't need to try
      //matching the rest of the query clause.
      continue;
    }
    newMatches|=lm->doEagerMatching();
  }
  (void)newMatches; // to prevent a warning
  return matchGlobalVars(resolvedQueryLit);
//  return newMatches && matchGlobalVars(resolvedQueryLit);
}

template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::ClauseMatcher::matchGlobalVars(int& resolvedQueryLit)
{
  //TODO: perform _set_, not _multiset_ subsumption for subsumption resolution

//  bool verbose=query->number()==58746;
//#define VERB_OUT(x) if(verbose) {cout<<x<<endl;}

  unsigned clen=lms.size()-1;

  //remaining[j,0] contains number of matches for j-th index literal
  //remaining[j,i+1] (for j>i) contains number of matches for j-th
  //  index literal compatible with the bindings of i-th literal (and
  //  those before it)
  //remaining[j,j] therefore contains number of matches we have to try
  //  when we get to binding j-th literal
  //  Matches in ILStruct::matches are reordered, so that we always try
  //  the _first_ remaining[j,j] literals
  static TriangularArray<int> remaining(10);
  remaining.setSide(clen);
  for(unsigned j=0;j<clen;j++) {
    ILStruct* ils=lms[j]->getILS();
    remaining.set(j,0,ils->matchCnt);

//    VERB_OUT("matches "<<ils->matches.size()<<" index:"<<j<<" vars:"<<ils->varCnt<<" linfos:"<<lInfos.size());
//    for(unsigned y=0;y<ils->matches.size();y++) {
//      LitInfo* linf=&lInfos[ils->matches[y]->liIndex];
//      VERB_OUT(" match "<<y<<" liIndex:"<<ils->matches[y]->liIndex<<" op: "<<linf->opposite);
//      VERB_OUT(" hdr: "<<(*linf->ft)[0].number());
//    }
//    for(unsigned x=0;x<ils->varCnt;x++) {
//      VERB_OUT(" glob var: "<<ils->sortedGlobalVarNumbers[x]);
//      for(unsigned y=0;y<ils->matches.size();y++) {
//	VERB_OUT(" match "<<y<<" binding: "<<ils->matches[y]->bindings[x]);
//      }
//    }
  }
//  VERB_OUT("secOp:"<<(lms[1]->op-1)->instr()<<" "<<(lms[1]->op-1)->arg());

  static DArray<int> matchIndex;
  matchIndex.ensure(clen);
  unsigned failLev=0;
  for(unsigned i=0;i<clen;i++) {
    matchIndex[i]=-1;
    if(i>0) { failLev=20; }
  bind_next_match:
    matchIndex[i]++;
    if(matchIndex[i]==remaining.get(i,i)) {
      //no more choices at this level, so try going up
      if(i==0) {
	RSTAT_MCTR_INC("zero level fails at", failLev);
	return false;
      }
      i--;
      goto bind_next_match;
    }
    ASS_L(matchIndex[i],remaining.get(i,i));

    ILStruct* bi=lms[i]->getILS(); 		//bound index literal
    MatchInfo* bq=bi->getMatch(matchIndex[i]);	//bound query literal

    //propagate the implications of this binding to next literals
    for(unsigned j=i+1;j<clen;j++) {
      ILStruct* ni=lms[j]->getILS();		//next index literal
      unsigned rem=remaining.get(j,i);
      unsigned k=0;
      while(k<rem) {
	MatchInfo* nq=ni->getMatch(k);		//next query literal
	if(!compatible(bi,bq,ni,nq)) {
	  rem--;
	  swap(ni->getMatch(k),ni->getMatch(rem));
	  continue;
	}
	k++;
      }
      if(rem==0) {
	if(failLev<j) { failLev=j; }
	goto bind_next_match;
      }
      remaining.set(j,i+1,rem);
    }
  }

  resolvedQueryLit=-1;
  if(sres) {
    for(unsigned i=0;i<clen;i++) {
      ILStruct* ils=lms[i]->getILS();
      MatchInfo* mi=ils->getMatch(matchIndex[i]);
      if(lInfos[mi->liIndex].opposite) {
	resolvedQueryLit=lInfos[mi->liIndex].litIndex;
	break;
      }
    }
  }

  return true;
}

template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::ClauseMatcher::compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq)
{
  if( lInfos[bq->liIndex].litIndex==lInfos[nq->liIndex].litIndex ||
      (lInfos[bq->liIndex].opposite && lInfos[nq->liIndex].opposite) ) {
    return false;
  }

  unsigned bvars=bi->varCnt;
  unsigned* bgvn=bi->sortedGlobalVarNumbers;
  TermList* bb=bq->bindings;

  unsigned nvars=ni->varCnt;
  unsigned* ngvn=ni->sortedGlobalVarNumbers;
  TermList* nb=nq->bindings;

  while(bvars && nvars) {
    while(bvars && *bgvn<*ngvn) {
      bvars--;
      bgvn++;
      bb++;
    }
    if(!bvars) {
      break;
    }
    while(nvars && *bgvn>*ngvn) {
      nvars--;
      ngvn++;
      nb++;
    }
    while(bvars && nvars && *bgvn==*ngvn) {
      if(*bb!=*nb) {
	return false;
      }
      bvars--;
      bgvn++;
      bb++;
      nvars--;
      ngvn++;
      nb++;
    }
  }

  return true;
}

template<bool higherOrder>
bool ClauseCodeTree<higherOrder>::ClauseMatcher::existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* targets)
{
  size_t tcnt=targets->matchCnt;
  for(size_t i=0;i<tcnt;i++) {
    if(compatible(si,sq,targets,targets->getMatch(i))) {
      return true;
    }
  }
  return false;
}


////////// OptimizedClauseMatcher

/**
 * Initialize the OptimizedClauseMatcher to retrieve generalizetions
 * of the @b query_ clause.
 * If @b sres_ if true, we perform subsumption resolution
 */
template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::init(OptimizedClauseCodeTree* tree_, Clause* query_, bool sres_)
{
  ASS(!tree_->isEmpty());

  query=query_;
  tree=tree_;
  sres=sres_;
  lms.reset();

#if VDEBUG
  ASS_EQ(tree->_clauseMatcherCounter,0);
  tree->_clauseMatcherCounter++;
#endif

  //init LitInfo records
  unsigned clen=query->length();
  unsigned baseLICnt=clen;
  for(unsigned i=0;i<clen;i++) {
    if((*query)[i]->isEquality()) {
      baseLICnt++;
    }
  }
  unsigned liCnt=sres ? (baseLICnt*2) : baseLICnt;
  lInfos.ensure(liCnt);

  //we put ground literals first
  unsigned liIndex=0;
  for(unsigned i=0;i<clen;i++) {
    if(!(*query)[i]->ground()) {
      continue;
    }
    lInfos[liIndex]=LitInfo(query,i);
    lInfos[liIndex].liIndex=liIndex;
    liIndex++;
    if((*query)[i]->isEquality()) {
      lInfos[liIndex]=LitInfo::getReversed(lInfos[liIndex-1]);
      lInfos[liIndex].liIndex=liIndex;
      liIndex++;
    }
  }
  for(unsigned i=0;i<clen;i++) {
    if((*query)[i]->ground()) {
      continue;
    }
    lInfos[liIndex]=LitInfo(query,i);
    lInfos[liIndex].liIndex=liIndex;
    liIndex++;
    if((*query)[i]->isEquality()) {
      lInfos[liIndex]=LitInfo::getReversed(lInfos[liIndex-1]);
      lInfos[liIndex].liIndex=liIndex;
      liIndex++;
    }
  }
  if(sres) {
    for(unsigned i=0;i<baseLICnt;i++) {
      unsigned newIndex=i+baseLICnt;
      lInfos[newIndex]=LitInfo::getOpposite(lInfos[i]);
      lInfos[newIndex].liIndex=newIndex;
    }
    sresLiteral=sresNoLiteral;
  }

  tree->incTimeStamp();
  futureCache.reset();
  enterLiteral(tree->getEntryPoint(), 0, clen==0, nullptr);
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::reset()
{
  unsigned liCnt=lInfos.size();
  for(unsigned i=0;i<liCnt;i++) {
    lInfos[i].dispose();
  }
  lms.reset();
  futureCache.reset();

#if VDEBUG
  ASS_EQ(tree->_clauseMatcherCounter,1);
  tree->_clauseMatcherCounter--;
#endif
}

/**
 * Return next clause matching query or 0 if there is not such
 */
template<bool higherOrder>
Clause* OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::next(int& resolvedQueryLit)
{
  TIME_TRACE("Optimized clause matcher next")
  if(lms.isEmpty()) {
    return 0;
  }

  for(;;) {
    OptimizedLiteralMatcher* lm = &*lms.top();

    //get next literal from the literal matcher
    bool found=lm->next();

    //if there's none, go one level up (or fail if at the top)
    if(!found) {
      leaveLiteral();
      if(lms.isEmpty()) {
	return 0;
      }
    }
    else if(lm->op->isSuccess()) {
      Clause* candidate=lm->op->template getSuccessResult<Clause>();
      RSTAT_MCTR_INC("candidates", lms.size()-1);
      if(checkCandidate(candidate, resolvedQueryLit)) {
	RSTAT_MCTR_INC("candidates (success)", lms.size()-1);
	return candidate;
      }
    }
    else if(canEnterLiteral(lm->op)) {
      ASS(lm->op->isLitEnd());
      ASS_LE(lms.size(), query->length()); //this is due to the seekOnlySuccess part below

      bool seekOnlySuccess=lm->getILS()->depth+1==query->length();
      //LIT_END ops without a successor have no code of their own to enter;
      //the next literal matcher may still pick up its matches from
      //futureCache, so we still enter it, just without any code to execute.
      CodeOp* newLitEntry = lm->getILS()->hasSuccessor ? lm->op+1 : nullptr;

      //check that we have cleared the sresLiteral value if it is no longer valid
      ASS(!sres || sresLiteral==sresNoLiteral || sresLiteral<lms.size()-1);

      if(sres && sresLiteral==sresNoLiteral) {
	//we check whether we haven't matched only opposite literals on the previous level
	if(lm->getILS()->noNonOppositeMatches) {
	  sresLiteral=lms.size()-1;
	}
      }

      enterLiteral(newLitEntry, lm->op->getILS()->depth+1, seekOnlySuccess, lm->op);
    }
  }
}

template<bool higherOrder>
inline bool OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::canEnterLiteral(CodeOp* op)
{
  ASS(op->isLitEnd());
  ASS_EQ(lms.top()->op, op);

  ILStruct* ils=op->getILS();
  if(ils->timestamp==tree->_curTimeStamp && ils->visited) {
    return false;
  }

  if (ils->depth+1 > query->length()) {
    return false;
  }

  ASS_EQ(ils->timestamp, tree->_curTimeStamp);
  ASS_L(ils->lmsIndex, lms.size());
  if (!lms[ils->lmsIndex]->eagerlyMatched()) {
    lms[ils->lmsIndex]->doEagerMatching();
  }
  for (ILStruct* prevILS = ils->previous; prevILS; prevILS = prevILS->previous) {
    ASS_EQ(prevILS->timestamp, tree->_curTimeStamp);
    ASS_L(prevILS->lmsIndex, lms.size());
    if (!lms[prevILS->lmsIndex]->eagerlyMatched()) {
      lms[prevILS->lmsIndex]->doEagerMatching();
    }
    size_t matchIndex=ils->matchCnt;
    while(matchIndex!=0) {
      matchIndex--;
      MatchInfo* mi=ils->getMatch(matchIndex);
      if(!existsCompatibleMatch(ils, mi, prevILS)) {
        ils->deleteMatch(matchIndex); //decreases ils->matchCnt
      }
    }
    if(!ils->matchCnt) {
      return false;
    }
  }

  return true;
}

/**
 * Enter literal matching starting at @c entry.
 *
 * @param entry the code tree node
 * @param seekOnlySuccess if true, accept only SUCCESS operations
 *   (this is to be used when all literals are matched so we want
 *   to see just clauses that end at this point).
 */
template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::enterLiteral(CodeOp* entry, unsigned depth, bool seekOnlySuccess, CodeOp* prevLitEnd)
{
  if(!seekOnlySuccess) {
    RSTAT_MCTR_INC("enterLiteral levels (non-sos)", lms.size());
  }

  if(lms.isNonEmpty()) {
    Recycled<OptimizedLiteralMatcher, NoReset>& prevLM = lms.top();
    ILStruct* ils=prevLM->op->getILS();
    ASS_EQ(ils->timestamp,tree->_curTimeStamp);
    ASS(!ils->visited);
    ASS(!ils->finished);
    ils->visited=true;
  }

  size_t linfoCnt=lInfos.size();
  if(sres && sresLiteral!=sresNoLiteral) {
    ASS_L(sresLiteral,lms.size());
    //we do not need to match index literals with opposite query
    //literals, as one of already matched index literals matched only
    //to opposite literals (and opposite literals cannot be matched
    //on more than one index literal)
    ASS_EQ(linfoCnt%2,0);
    linfoCnt/=2;
  }

  Recycled<OptimizedLiteralMatcher, NoReset> lm;
  lm->init(tree, entry, lInfos.array(), linfoCnt, depth, lms.size(), &futureCache, prevLitEnd, seekOnlySuccess);
  lms.push(std::move(lm));
}

template<bool higherOrder>
void OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::leaveLiteral()
{
  ASS(lms.isNonEmpty());

  lms.pop();

  if(lms.isNonEmpty()) {
    OptimizedLiteralMatcher* prevLM = &*lms.top();
    ILStruct* ils=prevLM->op->getILS();
    ASS_EQ(ils->timestamp,tree->_curTimeStamp);
    ASS(ils->visited);

    ils->finished=true;

    if(sres) {
      //clear the resolved literal flag if we have backtracked from it
      unsigned depth=lms.size()-1;
      if(sresLiteral==depth) {
        sresLiteral=sresNoLiteral;
      }
      ASS(sresLiteral==sresNoLiteral || sresLiteral<depth);
    }
  }
}


//////////////// Optimized multi-literal matching

template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::checkCandidate(Clause* cl, int& resolvedQueryLit)
{
  ASS(lms.length() >= 2);
  unsigned clen=cl->length();
  //the last matcher in mls is the one that yielded the SUCCESS operation
  //ASS_EQ(clen, lms.size()-1);
  unsigned ilsIdx = lms.length() - 2;
  ASS_EQ(clen, lms[ilsIdx]->op->getILS()->depth+1);

  if(clen<=1) {
    //if clause doesn't have multiple literals, there is no need
    //for multi-literal matching
    resolvedQueryLit=-1;
    if(sres && clen==1) {
      size_t matchCnt=lms[0]->getILS()->matchCnt;
      for(size_t i=0;i<matchCnt;i++) {
	MatchInfo* mi=lms[0]->getILS()->getMatch(i);
	if(lInfos[mi->liIndex].opposite) {
	  resolvedQueryLit=lInfos[mi->liIndex].litIndex;
	}
	else {
	  //we prefer subsumption to subsumption resolution
	  resolvedQueryLit=-1;
	  break;
	}
      }
    }
    return true;
  }

//  if(matchGlobalVars(resolvedQueryLit)) {
//    return true;
//  }

  return matchGlobalVars(resolvedQueryLit);
//  return newMatches && matchGlobalVars(resolvedQueryLit);
}

template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::matchGlobalVars(int& resolvedQueryLit)
{
  //TODO: perform _set_, not _multiset_ subsumption for subsumption resolution

//  bool verbose=query->number()==58746;
//#define VERB_OUT(x) if(verbose) {cout<<x<<endl;}

  ASS(lms.length() >= 2);
  ILStruct* lastIls = lms[lms.length() - 2]->getILS();
  unsigned clen=lastIls->depth+1;

  static DArray<ILStruct*> ilss;
  ilss.ensure(clen);

  int ilssIndex = clen-1;
  for (ILStruct* ils = lastIls; ils; ils=ils->previous, ilssIndex--) {
    ASS(ilssIndex >= 0);
    ilss[ilssIndex] = ils;
  }

  //remaining[j,0] contains number of matches for j-th index literal
  //remaining[j,i+1] (for j>i) contains number of matches for j-th
  //  index literal compatible with the bindings of i-th literal (and
  //  those before it)
  //remaining[j,j] therefore contains number of matches we have to try
  //  when we get to binding j-th literal
  //  Matches in ILStruct::matches are reordered, so that we always try
  //  the _first_ remaining[j,j] literals
  static TriangularArray<int> remaining(10);
  remaining.setSide(clen);
  for(unsigned j=0;j<clen;j++) {
    ILStruct* ils=ilss[j];
    remaining.set(j,0,ils->matchCnt);

//    VERB_OUT("matches "<<ils->matches.size()<<" index:"<<j<<" vars:"<<ils->varCnt<<" linfos:"<<lInfos.size());
//    for(unsigned y=0;y<ils->matches.size();y++) {
//      LitInfo* linf=&lInfos[ils->matches[y]->liIndex];
//      VERB_OUT(" match "<<y<<" liIndex:"<<ils->matches[y]->liIndex<<" op: "<<linf->opposite);
//      VERB_OUT(" hdr: "<<(*linf->ft)[0].number());
//    }
//    for(unsigned x=0;x<ils->varCnt;x++) {
//      VERB_OUT(" glob var: "<<ils->sortedGlobalVarNumbers[x]);
//      for(unsigned y=0;y<ils->matches.size();y++) {
//	VERB_OUT(" match "<<y<<" binding: "<<ils->matches[y]->bindings[x]);
//      }
//    }
  }
//  VERB_OUT("secOp:"<<(lms[1]->op-1)->instr()<<" "<<(lms[1]->op-1)->arg());

  static DArray<int> matchIndex;
  matchIndex.ensure(clen);
  unsigned failLev=0;
  for(unsigned i=0;i<clen;i++) {
    matchIndex[i]=-1;
    if(i>0) { failLev=20; }
  bind_next_match:
    matchIndex[i]++;
    if(matchIndex[i]==remaining.get(i,i)) {
      //no more choices at this level, so try going up
      if(i==0) {
	RSTAT_MCTR_INC("zero level fails at", failLev);
	return false;
      }
      i--;
      goto bind_next_match;
    }
    ASS_L(matchIndex[i],remaining.get(i,i));

    ILStruct* bi=ilss[i]; 		//bound index literal
    MatchInfo* bq=bi->getMatch(matchIndex[i]);	//bound query literal

    //propagate the implications of this binding to next literals
    for(unsigned j=i+1;j<clen;j++) {
      ILStruct* ni=ilss[j];		//next index literal
      unsigned rem=remaining.get(j,i);
      unsigned k=0;
      while(k<rem) {
	MatchInfo* nq=ni->getMatch(k);		//next query literal
	if(!compatible(bi,bq,ni,nq)) {
	  rem--;
	  swap(ni->getMatch(k),ni->getMatch(rem));
	  continue;
	}
	k++;
      }
      if(rem==0) {
	if(failLev<j) { failLev=j; }
	goto bind_next_match;
      }
      remaining.set(j,i+1,rem);
    }
  }

  resolvedQueryLit=-1;
  if(sres) {
    for(unsigned i=0;i<clen;i++) {
      ILStruct* ils=ilss[i];
      MatchInfo* mi=ils->getMatch(matchIndex[i]);
      if(lInfos[mi->liIndex].opposite) {
	resolvedQueryLit=lInfos[mi->liIndex].litIndex;
	break;
      }
    }
  }

  return true;
}

template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq)
{
  if( lInfos[bq->liIndex].litIndex==lInfos[nq->liIndex].litIndex ||
      (lInfos[bq->liIndex].opposite && lInfos[nq->liIndex].opposite) ) {
    return false;
  }

  unsigned bvars=bi->varCnt;
  unsigned* bgvn=bi->sortedGlobalVarNumbers;
  TermList* bb=bq->bindings;

  unsigned nvars=ni->varCnt;
  unsigned* ngvn=ni->sortedGlobalVarNumbers;
  TermList* nb=nq->bindings;

  while(bvars && nvars) {
    while(bvars && *bgvn<*ngvn) {
      bvars--;
      bgvn++;
      bb++;
    }
    if(!bvars) {
      break;
    }
    while(nvars && *bgvn>*ngvn) {
      nvars--;
      ngvn++;
      nb++;
    }
    while(bvars && nvars && *bgvn==*ngvn) {
      if(*bb!=*nb) {
	return false;
      }
      bvars--;
      bgvn++;
      bb++;
      nvars--;
      ngvn++;
      nb++;
    }
  }

  return true;
}

template<bool higherOrder>
bool OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher::existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* targets)
{
  size_t tcnt=targets->matchCnt;
  for(size_t i=0;i<tcnt;i++) {
    if(compatible(si,sq,targets,targets->getMatch(i))) {
      return true;
    }
  }
  return false;
}

template class ClauseCodeTree<false>;
template class ClauseCodeTree<true>;
template class OptimizedClauseCodeTree<false>;
template class OptimizedClauseCodeTree<true>;

}
