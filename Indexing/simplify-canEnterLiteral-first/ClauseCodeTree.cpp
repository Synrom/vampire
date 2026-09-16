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
#include "Debug/TimeProfiling.hpp"

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
namespace Ablation {
namespace SimplifyCanEnterFirst {

using namespace std;
using namespace Lib;
using namespace Kernel;

void ClauseCodeTree::printSuccess(std::ostream& out, const CodeOp& op) const
{
  out << op.getSuccessResult<Clause>()->toString();
}

ClauseCodeTree::ClauseCodeTree()
{
  _clauseCodeTree = true;
#if VDEBUG
  _clauseMatcherCounter=0;
#endif
}

//////////////// insertion ////////////////////

void ClauseCodeTree::insert(Clause* cl)
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
  }
  code.push(CodeOp::getSuccess(cl));

  compiler.updateCodeTree(this);

  incorporate(code);
  ASS(code.isEmpty());
}

struct ClauseCodeTree::InitialLiteralOrderingComparator
{
  Comparison compare(Literal* l1, Literal* l2)
  {
    if(l1->weight()!=l2->weight()) {
      return Int::compare(l2->weight(), l1->weight());
    }
    return Int::compare(l1->getId(), l2->getId());
  }
};

// LIT_END carries clause-wide variable information, not part of an overlap
unsigned ClauseCodeTree::sharedPrefix(const CodeOp* first, const CodeOp* second)
{
  unsigned shared = 0;
  while (!first[shared].isLitEnd() && !second[shared].isLitEnd() &&
         first[shared].equalsForOpMatching(second[shared])) {
    ++shared;
  }
  return shared;
}

ClauseCodeTree::InsertionPosition ClauseCodeTree::matchCode(
    const CodeOp* code, unsigned length, const Stack<InsertionPosition>& entries,
    unsigned shared, Stack<InsertionPosition>& nextEntries, bool compress)
{
  InsertionPosition best;
  nextEntries.reset();
  for (auto entry : entries) {
    if (entry.matchedPrefixLength > shared) {
      continue;
    }
    Stack<std::pair<unsigned, unsigned>> checkpoints;
    CodeOp* op = entry.op;
    unsigned i = entry.matchedPrefixLength;
    for (; i < length; ++i) {
      CodeOp* chainStart = op;
      unsigned funs = 0, grounds = 0;
      for (;;) {
        if (op->isNext()) {
          checkpoints.push({op->_arg(), i});
          ++op;
          continue;
        }
        if (op->isSearchStruct()) {
          CodeOp** target;
          if (op->getSearchStruct()->getTargetOpPtr<false>(code[i], target) && *target) {
            op = *target;
            entry.blockReference = target;
            entry.block = firstOpToCodeBlock(op);
            continue;
          }
        } else if (code[i].equalsForOpMatching(*op)) {
          break;
        }
        if (!op->alternative()) {
          if (i >= best.matchedPrefixLength) {
            best = {op, entry.block, entry.blockReference, i};
          }
          goto next_entry;
        }
        if (compress && op->alternative()->isCheckFun() && ++funs > 5) {
          compressCheckOps<SearchStruct::FN_STRUCT>(chainStart);
        } else if (compress && op->alternative()->isCheckGroundTerm() && ++grounds > 3) {
          compressCheckOps<SearchStruct::GROUND_TERM_STRUCT>(chainStart);
        } else {
          entry.blockReference = &op->alternative();
          op = *entry.blockReference;
          if (!op->isSearchStruct()) {
            entry.block = firstOpToCodeBlock(op);
          }
          continue;
        }
        op = chainStart;
        funs = grounds = 0;
      }
      if (i + 1 == length) {
        ASS(op->isLitEnd());
        for (auto& cont : op->getILS()->continuations) {
          for (auto checkpoint : checkpoints) {
            if (checkpoint.first == cont.slot) {
              nextEntries.push({cont.entry, firstOpToCodeBlock(cont.entry), &cont.entry, checkpoint.second});
              break;
            }
          }
        }
        if (op->hasSuccessor()) {
          nextEntries.push({op + 1, entry.block, entry.blockReference, 0});
        }
        return {op, entry.block, entry.blockReference, length};
      }
      ASS(op->hasSuccessor());
      ++op;
    }
next_entry:;
  }
  return best;
}

void ClauseCodeTree::incorporate(CodeStack& code)
{
  // Empty clauses contain only SUCCESS and need no literal-prefix matching.
  if (code.length() == 1) {
    CodeTree::incorporate(code);
    return;
  }

  ASS(code.top().isSuccess());
  Stack<unsigned> starts, overlaps;
  starts.push(0);
  for (unsigned i = 0; i + 1 < code.length(); ++i) {
    if (code[i].isLitEnd()) {
      starts.push(i + 1);
    }
  }
  unsigned clen = starts.length() - 1;
  ASS_G(clen, 0);
  for (unsigned i = 0; i < clen; ++i) {
    overlaps.push(i + 1 < clen ? sharedPrefix(&code[starts[i]], &code[starts[i+1]]) : 0);
  }

  Stack<InsertionPosition> entries, nextEntries;
  if (!isEmpty()) {
    entries.push({getEntryPoint(), getEntryBlock(), &_entryPoint, 0});
  }
  unsigned lit = 0, matchedCnt = 0;
  ILStruct* previous = nullptr;
  CodeOp** tailTarget = &_entryPoint;
  CodeBlock* append = nullptr;
  InsertionPosition last;
  for (; lit < clen && !isEmpty(); ++lit) {
    auto match = matchCode(&code[starts[lit]], starts[lit+1] - starts[lit], entries,
                           lit ? overlaps[lit-1] : 0, nextEntries, true);
    matchedCnt = starts[lit] + match.matchedPrefixLength;
    if (!match.op) {
      // The preceding LIT_END has no compatible continuation.
      ASS(previous);
      append = last.block;
      tailTarget = last.blockReference;
      break;
    }
    if (matchedCnt != starts[lit+1]) {
      tailTarget = &match.op->alternative();
      break;
    }
    previous = match.op->getILS();
    ++previous->refCount;
    last = match;
    entries = std::move(nextEntries);
  }
  if (lit == clen) {
    if (previous->hasSuccessor) {
      CodeOp* op = last.op + 1;
      while (op->alternative()) {
        op = op->alternative();
      }
      tailTarget = &op->alternative();
    } else {
      append = last.block;
      tailTarget = last.blockReference;
    }
  }

  // Build the unmatched suffix. A selected NEXT terminates a block at LIT_END
  // and redirects the next block into that literal end's bin
  static const unsigned int nextThreshold = 7;
  for (unsigned pos = matchedCnt; pos < code.length();) {
    CodeStack suffix;
    unsigned nextPosition = 0;
    ILStruct* split = nullptr;
    bool useNext = false;
    for (unsigned i = pos; i < code.length(); ++i) {
      if (lit < clen && overlaps[lit] > nextThreshold && i == starts[lit] + overlaps[lit]) {
        nextPosition = suffix.length();
        suffix.push(CodeOp::getNext(0));
        useNext = true;
      }
      suffix.push(code[i]);
      if (code[i].isLitEnd()) {
        code[i].getILS()->hasSuccessor = true;
        if (useNext) {
          split = code[i].getILS();
          pos = starts[lit+1] + overlaps[lit];
          ++lit;
          break;
        }
        ++lit;
      }
      pos = i + 1;
    }
    CodeBlock* block = CodeTree::buildBlock(suffix, suffix.length(), previous);
    if (append) {
      unsigned offset = append->length();
      CodeBlock* joined = CodeBlock::allocate(offset + block->length());
      for (unsigned i = 0; i < offset; ++i) {
        (*joined)[i] = (*append)[i];
      }
      for (unsigned i = 0; i < block->length(); ++i) {
        (*joined)[offset+i] = (*block)[i];
      }
      previous->hasSuccessor = true;
      nextPosition += offset;
      append->deallocate();
      block->deallocate();
      block = joined;
      append = nullptr;
    }
    *tailTarget = &(*block)[0];
    if (split) {
      split->hasSuccessor = false;
      unsigned slot = split->previous ? split->previous->successorSlotCount++ : firstLiteralSlotCount++;
      (*block)[nextPosition]._setArg(slot);
      split->continuations.push({slot, nullptr});
      split->hasContinuations = true;
      tailTarget = &split->continuations.top().entry;
      previous = split;
    }
  }

  // Discard only the unused shared prefix
  for (unsigned i = 0; i < matchedCnt; ++i) {
    if (code[i].isLitEnd()) {
      delete code[i].getILS();
    }
  }
  code.reset();
}

void ClauseCodeTree::optimizeLiteralOrder(DArray<Literal*>& lits)
{
  lits.sort(InitialLiteralOrderingComparator());
  unsigned clen=lits.size();
  if (clen < 2) {
    return;
  }

  CodeStack code;
  LitCompiler compiler(code);
  DArray<CodeStack> codes(clen);
  for (unsigned i = 0; i < clen; ++i) {
    compiler.nextLit();
    compiler.handleTerm(lits[i]);
    codes[i] = std::move(code);
  }

  Stack<InsertionPosition> entries, nextEntries;
  if (!isEmpty()) {
    entries.push({getEntryPoint(), getEntryBlock(), &_entryPoint, 0});
  }
  unsigned start = 0;
  for (; start + 1 < clen && entries.isNonEmpty(); start++) {
    unsigned best = start;
    unsigned bestShared = 0;
    bool complete = false;
    for (unsigned i = start; i < clen; i++) {
      unsigned prefix = start ? sharedPrefix(codes[start-1].begin(), codes[i].begin()) : 0;
      auto match = matchCode(codes[i].begin(), codes[i].length(), entries, prefix, nextEntries, false);
      if (match.matchedPrefixLength == codes[i].length()) {
        best = i;
        complete = true;
        break;
      }
      if (match.matchedPrefixLength > bestShared && (!lits[best]->ground() || lits[i]->ground())) {
        best = i;
        bestShared = match.matchedPrefixLength;
      }
    }
    std::swap(lits[start], lits[best]);
    std::swap(codes[start], codes[best]);
    if (!complete) {
      break;
    }
    entries = std::move(nextEntries);
  }

  // Beyond the shared clause prefix, favour consecutive literal overlaps.
  for (; start + 1 < clen; start++) {
    unsigned best = start + 1;
    unsigned bestShared = sharedPrefix(codes[start].begin(), codes[best].begin());
    for (unsigned i = best + 1; i < clen; ++i) {
      unsigned shared = sharedPrefix(codes[start].begin(), codes[i].begin());
      if (shared > bestShared && (!lits[best]->ground() || lits[i]->ground())) {
        best = i;
        bestShared = shared;
      }
    }
    std::swap(lits[start+1], lits[best]);
    std::swap(codes[start+1], codes[best]);
  }
  for (auto& literal : codes) {
    delete literal.top().getILS();
  }
}

//////////////// removal ////////////////////

void ClauseCodeTree::remove(Clause* cl)
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
      ILStruct* ils = nullptr;
      if (rlms->size() > 0) {
        rlms->top()->doEagerMatching();
        ils = rlms->top()->op->getILS();
      }
      CodeOp* newLitEntry;
      if (ils) {
        newLitEntry = ils->hasSuccessor ? rlms->top()->op + 1 : nullptr;
      } else {
        newLitEntry = getEntryPoint();
      }
      rlm->init(newLitEntry, lInfos.array(), lInfos.size(), this, &*firstsInBlocks,
          ils ? ils->successorSlotCount : firstLiteralSlotCount,
          ils ? &ils->continuations : nullptr, ils ? &*rlms->top() : nullptr);
      rlms->push(std::move(rrlm)); // store it in rlms (along with the obligation to return to recycling when no longer used)
    }

  iteration_restart:
    if(!rlm->next()) {
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

    if(depth==clen-1) {
      if(op->hasSuccessor() && removeOneOfAlternatives(op+1, cl, &*firstsInBlocks)) {
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

void ClauseCodeTree::RemovingLiteralMatcher::doEagerMatching()
{
  if (_eagerlyMatched) return;

  ASS(eagerResults.isEmpty());

  //backup the current op
  CodeOp* currOp=op;
  Stack<CodeOp*> currFirstInBlocks = *RemovingBase::firstsInBlocks;

  while(execute()) {
    eagerResults.push(op);
    eagerResultsFirstInBlocks.push(*RemovingBase::firstsInBlocks);
  }

  _eagerlyMatched=true;

  op=currOp; //restore the current op
  *RemovingBase::firstsInBlocks = currFirstInBlocks;
}

void ClauseCodeTree::RemovingLiteralMatcher::init(CodeOp* entry_, LitInfo* linfos_,
    size_t linfoCnt_, ClauseCodeTree* tree_, Stack<CodeOp*>* firstsInBlocks_, unsigned slotCount,
    const Stack<ILStruct::Continuation>* continuations_, const Matcher</*removing*/true,false>* source_)
{
  Matcher::init(*tree_, entry_, linfos_, linfoCnt_, firstsInBlocks_, slotCount, continuations_, source_);
  eagerResults.reset();
  eagerResultsFirstInBlocks.reset();
  _eagerlyMatched = false;
  ALWAYS(prepareLiteral());
}

/**
 * The first operation of the CodeBlock containing @b op
 * must already be on the @b firstsInBlocks stack.
 */
bool ClauseCodeTree::removeOneOfAlternatives(CodeOp* op, Clause* cl, Stack<CodeOp*>* firstsInBlocks)
{
  unsigned initDepth=firstsInBlocks->size();

  while(!op->isSuccess() || op->getSuccessResult<Clause>()!=cl) {
    op=op->alternative();
    if(!op) {
      firstsInBlocks->truncate(initDepth);
      return false;
    }
    firstsInBlocks->push(op);
  }
  op->makeFail();
  optimizeMemoryAfterRemoval(firstsInBlocks, op);
  return true;
}

//////////////// retrieval ////////////////////

////////// LiteralMatcher

/**
 * If @b seekOnlySuccess if true, we will look only for immediate SUCCESS operations
 *  and fail if there isn't any at the beginning (possibly also among alternatives).
 */
void ClauseCodeTree::LiteralMatcher::init(const CodeTree& tree_, CodeOp* entry_,
					  LitInfo* linfos_, size_t linfoCnt_,
					  bool seekOnlySuccess,
            const Stack<CodeTree::ILStruct::Continuation>* continuations_,
            const Matcher</*removing*/false,false>* source_,
            unsigned slotCount)
{
  ASS_G(linfoCnt_,0);

  Matcher::init(tree_,entry_,linfos_,linfoCnt_, 0, slotCount, continuations_, source_);

  _eagerlyMatched=false;
  eagerResults.reset();

  RSTAT_CTR_INC("LiteralMatcher::init");
  if(seekOnlySuccess) {
    RSTAT_CTR_INC("LiteralMatcher::init - seekOnlySuccess");
    //we are interested only in SUCCESS operations
    //(and those must be at the entry point or its alternatives)

    _eagerlyMatched=true;
    fresh=false;
    CodeOp* sop=entry;
    while(sop) {
      if(sop->isSuccess()) {
        eagerResults.push(sop);
      }
      sop=sop->alternative();
    }
    return;
  }

  // Subsumption resolution pruning may leave no usable checkpoint to prepare
  prepareLiteral();
}

/**
 * Try to find a match, and if one is found, return true
 */
bool ClauseCodeTree::LiteralMatcher::next()
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

bool ClauseCodeTree::RemovingLiteralMatcher::next()
{
  if(_eagerlyMatched) {
    _matched=!eagerResults.isEmpty();
    if(!_matched) {
      return false;
    }
    op=eagerResults.pop();
    *RemovingBase::firstsInBlocks = eagerResultsFirstInBlocks.pop();
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
  return true;
}

/**
 * Perform eager matching and return true iff new matches were found
 */
bool ClauseCodeTree::LiteralMatcher::doEagerMatching()
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

void ClauseCodeTree::LiteralMatcher::recordMatch()
{
  ASS(_matched);

  ILStruct* ils=op->getILS();
  ils->ensureFreshness(tree->_curTimeStamp);
  if(ils->finished) {
    //no need to record matches which we already know will not lead to anything
    return;
  }
  if(!ils->matchCnt && linfos[curLInfo].opposite) {
    //if we're matching opposite matches, we have already tried all non-opposite ones
    ils->noNonOppositeMatches=true;
  } else if (ils->noNonOppositeMatches && !linfos[curLInfo].opposite) {
    ils->noNonOppositeMatches=false;
  }
  ils->addMatch(linfos[curLInfo].liIndex, bindings);
}


////////// ClauseMatcher

/**
 * Initialize the ClauseMatcher to retrieve generalizetions
 * of the @b query_ clause.
 * If @b sres_ if true, we perform subsumption resolution
 */
void ClauseCodeTree::ClauseMatcher::init(ClauseCodeTree* tree_, Clause* query_, bool sres_)
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
  enterLiteral(tree->getEntryPoint(), clen==0, nullptr);
}

void ClauseCodeTree::ClauseMatcher::reset()
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
Clause* ClauseCodeTree::ClauseMatcher::next(int& resolvedQueryLit)
{
  TIME_TRACE("Clause Matcher next simplifyCanEnterFirst")
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
      ILStruct* ils = lm->op->getILS();

      CodeOp* newLitEntry=ils->hasSuccessor ? lm->op+1 : nullptr;

      //check that we have cleared the sresLiteral value if it is no longer valid
      ASS(!sres || sresLiteral==sresNoLiteral || sresLiteral<lms.size()-1);

      if(sres && sresLiteral==sresNoLiteral) {
	//we check whether we haven't matched only opposite literals on the previous level
	if(ils->noNonOppositeMatches) {
	  sresLiteral=lms.size()-1;
	}
      }

      bool seekOnlySuccess=lms.size()==query->length();
      enterLiteral(newLitEntry, seekOnlySuccess, ils);
    }
  }
}

inline bool ClauseCodeTree::ClauseMatcher::canEnterLiteral(CodeOp* op)
{
  ASS(op->isLitEnd());
  ASS_EQ(lms.top()->op, op);

  ILStruct* ils=op->getILS();
  if(ils->timestamp==tree->_curTimeStamp && ils->visited) {
    return false;
  }

  if (ils->reachedByNextOp() && lms.size()!=query->length()) {
    LiteralMatcher* top = &*lms.top();
    // Record all checkpoints before checking or replaying continuations
    if(!top->eagerlyMatched()) {
      top->doEagerMatching();
      RSTAT_MST_INC("match count", lms.size()-1, top->getILS()->matchCnt);
    }
    if(!ils->hasSuccessor) {
      bool empty = true;
      for (const Continuation& cont: ils->continuations) {
        if (top->checkpointSlots[cont.slot].isNonEmpty()) {
          empty = false;
          break;
        }
      }
      if (empty) {
        // No successor or checkpoints remain; stop recording matches
        ils->visited=true;
        ils->finished=true;
        return false;
      }
    }
  }

  //we have already matched and entered some index literals, so we
  //will check for compatibility of variable assignments
  if(!lms.top()->eagerlyMatched()) {
    lms.top()->doEagerMatching();
    RSTAT_MST_INC("match count", lms.size()-1, lms.top()->getILS()->matchCnt);
  }
  for(size_t ilIndex=0;ilIndex<lms.size()-1;ilIndex++) {
    ILStruct* prevILS=lms[ilIndex]->getILS();
    if(!lms[ilIndex]->eagerlyMatched()) {
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
void ClauseCodeTree::ClauseMatcher::enterLiteral(CodeOp* entry, bool seekOnlySuccess, ILStruct* ils)
{
  if(!seekOnlySuccess) {
    RSTAT_MCTR_INC("enterLiteral levels (non-sos)", lms.size());
  }

  //the checkpoints of the new literal matcher are read from the previous literal matcher's continuations
  const Stack<Continuation>* continuations = nullptr;
  const Matcher</*removing*/false,false>* source = nullptr;
  unsigned slotCount = ils ? ils->successorSlotCount : tree->firstLiteralSlotCount;
  if(!seekOnlySuccess && ils && ils->reachedByNextOp()) {
    continuations = &ils->continuations;
    source = &*lms.top();
  }

  if(lms.isNonEmpty()) {
    Recycled<LiteralMatcher, NoReset>& prevLM = lms.top();
    ILStruct* prevIls=prevLM->op->getILS();
    ASS_EQ(prevIls->timestamp,tree->_curTimeStamp);
    ASS(!prevIls->visited);
    ASS(!prevIls->finished);
    prevIls->visited=true;
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
  lm->init(*tree, entry, lInfos.array(), linfoCnt, seekOnlySuccess, continuations, source, slotCount);
  lms.push(std::move(lm));
}

void ClauseCodeTree::ClauseMatcher::leaveLiteral()
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

bool ClauseCodeTree::ClauseMatcher::checkCandidate(Clause* cl, int& resolvedQueryLit)
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

bool ClauseCodeTree::ClauseMatcher::matchGlobalVars(int& resolvedQueryLit)
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

bool ClauseCodeTree::ClauseMatcher::compatible(ILStruct* bi, MatchInfo* bq, ILStruct* ni, MatchInfo* nq)
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

bool ClauseCodeTree::ClauseMatcher::existsCompatibleMatch(ILStruct* si, MatchInfo* sq, ILStruct* targets)
{
  size_t tcnt=targets->matchCnt;
  for(size_t i=0;i<tcnt;i++) {
    if(compatible(si,sq,targets,targets->getMatch(i))) {
      return true;
    }
  }
  return false;
}

}
}
}
