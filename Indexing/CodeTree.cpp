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
 * @file CodeTree.cpp
 * Implements class CodeTree.
 */

#include <algorithm>
#include <utility>

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Comparison.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"

#include "CodeTree.hpp"

#define ISATTY 1
#if ISATTY
#define CRESET "\e[0m"
#define YELLOW "\e[0;33m"
#define GREEN  "\e[0;32m"
#define RED    "\e[0;31m"
#else
#define CRESET ""
#define YELLOW ""
#define GREEN  ""
#define RED    ""
#endif

#define GROUND_TERM_CHECK 0

#undef RSTAT_COLLECTION
#define RSTAT_COLLECTION 0

namespace Indexing
{

#define GET_CONTAINING_OBJECT_CONST(ContainingClass,MemberField,object) \
  reinterpret_cast<const ContainingClass*>(reinterpret_cast<const char*>(object)-offsetof(ContainingClass,MemberField))

#define GET_CONTAINING_OBJECT(ContainingClass,MemberField,object) \
  reinterpret_cast<ContainingClass*>(reinterpret_cast<char*>(object)-offsetof(ContainingClass,MemberField))

using namespace std;
using namespace Lib;
using namespace Kernel;

//////////////// general datastructures ////////////////////

CodeTree::LitInfo::LitInfo(Clause* cl, unsigned litIndex)
: litIndex(litIndex), opposite(false)
{
  ft=FlatTerm::create(TermList((*cl)[litIndex]));
}

void CodeTree::LitInfo::dispose()
{
  ft->destroy();
}

CodeTree::LitInfo CodeTree::LitInfo::getReversed(const LitInfo& li)
{
  FlatTerm* ft=FlatTerm::copy(li.ft);
  ft->swapCommutativePredicateArguments();

  LitInfo res=li;
  res.ft=ft;
#if VDEBUG
  res.liIndex=-1; //the liIndex has to be updated by caller
#endif
  return res;
}

CodeTree::LitInfo CodeTree::LitInfo::getOpposite(const LitInfo& li)
{
  FlatTerm* ft=FlatTerm::copy(li.ft);
  ft->changeLiteralPolarity();
#if GROUND_TERM_CHECK
  ASS_EQ((*ft)[1]._tag(), FlatTerm::FUN_TERM_PTR);
  (*ft)[1]._setTerm(Literal::complementaryLiteral(static_cast<Literal*>((*ft)[1]._term())));
#endif

  LitInfo res=li;
  res.ft=ft;
  res.opposite=true;
#if VDEBUG
  res.liIndex=-1; //the liIndex has to be updated by caller
#endif
  return res;
}


/**
 * Allocate a MatchInfo object having @b bindCnt binding positions.
 */
CodeTree::MatchInfo* CodeTree::MatchInfo::alloc(unsigned bindCnt)
{
  void* mem=ALLOC_KNOWN(bytesRequiredFor(bindCnt),"CodeTree::MatchInfo");
  return reinterpret_cast<MatchInfo*>(mem);
}

/**
 * Destroy the MatchInfo object with @b bindCnt bindings
 */
void CodeTree::MatchInfo::destroy(unsigned bindCnt)
{
  DEALLOC_KNOWN(this, bytesRequiredFor(bindCnt),"CodeTree::MatchInfo");
}


void CodeTree::MatchInfo::init(ILStruct* ils, unsigned liIndex_, DArray<TermList>& bindingArray)
{
  liIndex=liIndex_;
  size_t bindCnt=ils->varCnt;
  if(bindCnt) {
    unsigned* perm=ils->globalVarPermutation;
    for(size_t i=0;i<bindCnt;i++) {
      bindings()[perm[i]]=bindingArray[i];
    }
  }
}


CodeTree::ILStruct::ILStruct(const Literal* lit, unsigned varCnt, Stack<unsigned>& gvnStack)
: varCnt(varCnt), sortedGlobalVarNumbers(0), globalVarPermutation(0), timestamp(0)
{
  ASS_EQ(matches.size(), 0); //we don't want any uninitialized pointers in the array

  if(varCnt) {
    size_t gvnSize=sizeof(unsigned)*varCnt;
    globalVarNumbers=static_cast<unsigned*>(
      ALLOC_KNOWN(gvnSize, "CodeTree::ILStruct::globalVarNumbers"));
    memcpy(globalVarNumbers, gvnStack.begin(), gvnSize);
  }
  else {
    globalVarNumbers=0;
  }
}

CodeTree::ILStruct::~ILStruct()
{
  size_t msize=matches.size();
  for(size_t i=0;i<msize;i++) {
    if(matches[i]) {
      matches[i]->destroy(varCnt);
    }
    else {
      //non-zero entries are only in the beginning of the matches array
      break;
    }
  }

  if(globalVarNumbers) {
    size_t gvSize=sizeof(unsigned)*varCnt;
    DEALLOC_KNOWN(globalVarNumbers, gvSize,
      "CodeTree::ILStruct::globalVarNumbers");
    if(sortedGlobalVarNumbers) {
      DEALLOC_KNOWN(sortedGlobalVarNumbers, gvSize,
        "CodeTree::ILStruct::sortedGlobalVarNumbers");
    }
    if(globalVarPermutation) {
      DEALLOC_KNOWN(globalVarPermutation, gvSize,
        "CodeTree::ILStruct::globalVarPermutation");
    }
  }
}

/**
 * Comparator used by the @b putIntoSequence function to order global
 * variable numbers
 */
struct CodeTree::ILStruct::GVArrComparator
{
  Comparison compare(const pair<unsigned,unsigned>& p1,
      const pair<unsigned,unsigned>& p2)
  {
    return Int::compare(p1.first, p2.first);
  }
};

/**
 * This function is called by the buildBlock function to make the
 * ILStruct object relate to its predecessors
 */
void CodeTree::ILStruct::putIntoSequence(ILStruct* previous_)
{
  depth=previous_ ? previous_->depth+1 : 0;

  if(!varCnt) { return; }

  static DArray<pair<unsigned,unsigned> > gvArr;
  gvArr.ensure(varCnt);
  for(unsigned i=0;i<varCnt;i++) {
    gvArr[i].first=globalVarNumbers[i];
    gvArr[i].second=i;
  }
  gvArr.sort(GVArrComparator());

  size_t gvSize=sizeof(unsigned)*varCnt;
  sortedGlobalVarNumbers=static_cast<unsigned*>(
    ALLOC_KNOWN(gvSize, "CodeTree::ILStruct::sortedGlobalVarNumbers"));
  globalVarPermutation=static_cast<unsigned*>(
    ALLOC_KNOWN(gvSize, "CodeTree::ILStruct::globalVarPermutation"));

  for(unsigned i=0;i<varCnt;i++) {
    sortedGlobalVarNumbers[i]=gvArr[i].first;
    globalVarPermutation[gvArr[i].second]=i;
  }
}

bool CodeTree::ILStruct::equalsForOpMatching(const ILStruct& o) const
{
  //LIT_END is always at the end of the term and we ask for op matching only
  //if the prefixes were equal. In this case the number of variables and the fact
  //the literal is an equality between variables should be the same on both literals.
  ASS_EQ(varCnt,o.varCnt);

  if(varCnt!=o.varCnt) {
    return false;
  }
  // avoids passing nullptr to memcmp below,
  // which is technically UB
  if(!varCnt)
    return true;

  ASS(globalVarNumbers)
  ASS(o.globalVarNumbers)
  return std::memcmp(globalVarNumbers, o.globalVarNumbers, varCnt * sizeof(unsigned)) == 0;
}

void CodeTree::ILStruct::ensureFreshness(unsigned globalTimestamp)
{
  if(timestamp!=globalTimestamp) {
    timestamp=globalTimestamp;
    visited=false;
    finished=false;
    noNonOppositeMatches=false;
    matchCnt=0;
  }
}

void CodeTree::ILStruct::addMatch(unsigned liIndex, DArray<TermList>& bindingArray)
{
  if(matchCnt==matches.size()) {
    matches.expand(matchCnt ? (matchCnt*2) : 4);
    size_t newSize=matches.size();
    for(size_t i=matchCnt;i<newSize;i++) {
      matches[i]=0;
    }
  }
  ASS_L(matchCnt,matches.size());
  if(!matches[matchCnt]) {
    matches[matchCnt]=MatchInfo::alloc(varCnt);
  }
  matches[matchCnt]->init(this, liIndex, bindingArray);
  matchCnt++;
}

/**
 * Remove match from the set of matches. It puts the last match in
 * the place of the current match. Therefore one should not rely on the
 * order of matches (at least those of index greater than matchIndex)
 * between calls to this function. When one traverses all the matches
 * to filter them by this function, the traversal should go from higher
 * indexes down to zero.
 */
void CodeTree::ILStruct::deleteMatch(unsigned matchIndex)
{
  ASS_L(matchIndex, matchCnt);

  matchCnt--;
  swap(matches[matchIndex], matches[matchCnt]);
}

CodeTree::MatchInfo*& CodeTree::ILStruct::getMatch(unsigned matchIndex)
{
  ASS(!finished);
  ASS_L(matchIndex, matchCnt);
  ASS(matches[matchIndex]);

  return matches[matchIndex];
}

const CodeTree::MatchInfo* CodeTree::ILStruct::getMatch(unsigned matchIndex) const
{
  ASS(!finished);
  ASS_L(matchIndex, matchCnt);
  ASS(matches[matchIndex]);

  return matches[matchIndex];
}

CodeTree::CodeOp CodeTree::CodeOp::getLitEnd(ILStruct* ils)
{
  CodeOp res;
  res._setData(ils);
  res._setInstruction(LIT_END);
  ASS(res.isLitEnd());
  return res;
}

CodeTree::CodeOp CodeTree::CodeOp::getTermOp(Instruction i, unsigned num)
{
  ASS(i==CHECK_FUN || i==CHECK_VAR || i==ASSIGN_VAR);

  CodeOp res;
  res._setInstruction(i);
  res._setArg(num);
  return res;
}

CodeTree::CodeOp CodeTree::CodeOp::getGroundTermCheck(const Term* trm)
{
  ASS(trm->ground());

  CodeOp res;
  res._setData(trm);
  res._setInstruction(CHECK_GROUND_TERM);
  ASS(res.isCheckGroundTerm());
  return res;
}

CodeTree::CodeOp CodeTree::CodeOp::getSearchStructOp(SearchStruct* ss)
{
  CodeOp res;
  res._setData(ss);
  res._setInstruction(SEARCH_STRUCT);
  ASS(res.isSearchStruct());
  return res;
}

/**
 * Return true iff @b o is equal to the object for the purpose
 * of operation matching during cide insertion into the tree
 */
bool CodeTree::CodeOp::equalsForOpMatching(const CodeOp& o) const
{
  if(_instruction()!=o._instruction()) {
    return false;
  }
  switch(_instruction()) {
  case LIT_END:
    return getILS()->equalsForOpMatching(*o.getILS());
  case SUCCESS_OR_FAIL:
  case CHECK_GROUND_TERM:
  case CHECK_FUN:
  case ASSIGN_VAR:
  case CHECK_VAR:
    return _content==o._content;
  default:
    //SEARCH_STRUCT operations in the tree should be handled separately
    //during insertion into the code tree
    ASSERTION_VIOLATION;
  }
}

const CodeTree::SearchStruct* CodeTree::CodeOp::getSearchStruct() const
{
  ASS(isSearchStruct());
  return _data<SearchStruct>();
}

CodeTree::SearchStruct* CodeTree::CodeOp::getSearchStruct()
{
  ASS(isSearchStruct());
  return _data<SearchStruct>();
}

//////////////// CodeTreeSubtree ////////////////////

CodeTree::CodeTreeSubtree* CodeTree::CodeTreeSubtree::create(uint32_t capacity)
{
  ASS_G(capacity,0);
  void* mem=ALLOC_KNOWN(bytesFor(capacity),"CodeTree::CodeTreeSubtree");
  auto res=reinterpret_cast<CodeTreeSubtree*>(mem);
  res->_size=0;
  res->_capacity=capacity;
  res->_garbage=0;
  res->_compactedSize=0;
  return res;
}

void CodeTree::CodeTreeSubtree::deallocate(CodeTreeSubtree* st)
{
  DEALLOC_KNOWN(st, bytesFor(st->_capacity), "CodeTree::CodeTreeSubtree");
}

uint32_t CodeTree::CodeTreeSubtree::append(CodeTreeSubtree*& st, uint32_t n)
{
  if(!st) {
    st=create(n);
    st->_compactedSize=n;
  }
  uint32_t res=st->_size;
  if(res+n>st->_capacity) {
    // grow geometrically, otherwise repeated insertions into
    // the same subtree would be quadratic
    uint32_t newCap=std::max(res+n, st->_capacity+st->_capacity/2);
    CodeTreeSubtree* nst=create(newCap);
    nst->_size=st->_size;
    nst->_garbage=st->_garbage;
    nst->_compactedSize=st->_compactedSize;
    CodeOp* oldOps=st->ops();
    CodeOp* newOps=nst->ops();
    memcpy(static_cast<void*>(newOps), oldOps, sizeof(CodeOp)*st->_size);
    //relocate the alternatives, which all point within the subtree
    for(uint32_t i=0;i<st->_size;i++) {
      if(newOps[i].alternative()) {
        newOps[i].setAlternative(newOps+(newOps[i].alternative()-oldOps));
      }
    }
    deallocate(st);
    st=nst;
  }
  CodeOp* ops=st->ops()+res;
  for(uint32_t i=0;i<n;i++) {
    new(ops+i) CodeOp();
  }
  st->_size+=n;
  return res;
}

std::string functorStr(unsigned functor, bool litStart)
{
  // TODO without slowing code trees down by adding new operations, it's hard to distinguish
  // between predicates/functions and type constructors, so we just avoid failure now.
  if (litStart) {
    if (env.signature->predicates() <= (functor / 2)) {
      return env.signature->getTypeCon(functor)->name();
    }
    return (functor % 2 == 0 ? "~" : "") + env.signature->getPredicate(functor / 2)->name();
  }

  if (env.signature->functions() <= functor) {
    return env.signature->getTypeCon(functor)->name();
  }
  return env.signature->getFunction(functor)->name();
}

void CodeTree::printOp(std::ostream& out, const CodeTree::CodeOp& op, bool litStart) const
{
  switch (op._instruction()) {
    case CodeTree::SUCCESS_OR_FAIL:
      if (op.isSuccess()) {
        out << GREEN << "success " << CRESET;
        printSuccess(out, op);
      } else {
        out << RED << "fail" << CRESET;
      }
      break;
    case CodeTree::LIT_END:
      out << GREEN << "lit end" << CRESET;
      break;
    case CodeTree::CHECK_GROUND_TERM:
      out << YELLOW << "ground " << CRESET << *op.getTargetTerm();
      break;
    case CodeTree::CHECK_FUN: {
      out << YELLOW << "check " << CRESET << functorStr(op._arg(), litStart);
      break;
    }
    case CodeTree::ASSIGN_VAR:
      out << YELLOW << "assign" << CRESET << " X" << op._arg();
      break;
    case CodeTree::CHECK_VAR:
      out << YELLOW << "check" << CRESET << " X" << op._arg();
      break;
    case CodeTree::SEARCH_STRUCT: {
      auto ss = op.getSearchStruct();
      out << YELLOW;
      if (ss->kind == CodeTree::SearchStruct::FN_STRUCT) {
        out << "fn";
      } else {
        out << "gt";
      }
      out << " search: " << CRESET;
      for (unsigned i = 0; i < ss->length(); i++) {
        if (ss->kind == CodeTree::SearchStruct::FN_STRUCT) {
          out << functorStr(static_cast<const CodeTree::FnSearchStruct*>(ss)->values[i], litStart);
        } else {
          out << *static_cast<const CodeTree::GroundTermSearchStruct*>(ss)->values[i];
        }
        if (i+1 < ss->length()) {
          out << ", ";
        }
      }
      break;
    }
  }
}

CodeTree::SearchStruct::SearchStruct(Kind kind, size_t length)
: kind(kind)
{
  ASS(length);

  targets.reserve(length);
}

void CodeTree::SearchStruct::destroy()
{
  switch(kind) {
  case FN_STRUCT:
    delete static_cast<FnSearchStruct*>(this);
    break;
  case GROUND_TERM_STRUCT:
    delete static_cast<GroundTermSearchStruct*>(this);
    break;
  }
}

template<bool doInsert>
bool CodeTree::SearchStruct::getTargetOpPtr(const CodeOp& insertedOp, int32_t*& tgt)
{
  switch(kind) {
  case FN_STRUCT:
    if(!insertedOp.isCheckFun()) { return false; }
    tgt=&static_cast<FnSearchStruct*>(this)->targetOp<doInsert>(insertedOp._arg());
    return true;
  case GROUND_TERM_STRUCT:
    if(!insertedOp.isCheckGroundTerm()) { return false; }
    tgt=&static_cast<GroundTermSearchStruct*>(this)->targetOp<doInsert>(insertedOp.getTargetTerm());
    return true;
  default:
    ASSERTION_VIOLATION;
  }
}

// expose for ClauseCodeTree.cpp
template bool CodeTree::SearchStruct::getTargetOpPtr<false>(const CodeOp&, int32_t*&);

CodeTree::CodeOp* CodeTree::SearchStruct::getTargetOp(CodeOp* ssOp, const FlatTerm::Entry* ftPos)
{
  if(!ftPos->isFun()) { return 0; }
  int32_t ofs;
  switch(kind) {
  case FN_STRUCT:
    ofs=static_cast<FnSearchStruct*>(this)->targetOp<false>(ftPos->_number());
    break;
  case GROUND_TERM_STRUCT:
    ftPos++;
    ASS_EQ(ftPos->_tag(), FlatTerm::FUN_TERM_PTR);
    ofs=static_cast<GroundTermSearchStruct*>(this)->targetOp<false>(ftPos->_term());
    break;
  default:
    ASSERTION_VIOLATION;
  }
  return ofs ? ssOp+ofs : nullptr;
}

template<CodeTree::SearchStruct::Kind k>
CodeTree::SearchStructImpl<k>::SearchStructImpl(size_t length)
: SearchStruct(k, length)
{
}

template<CodeTree::SearchStruct::Kind k>
template<bool doInsert>
int32_t& CodeTree::SearchStructImpl<k>::targetOp(const T& val)
{
  size_t left=0;
  size_t right=length()-1;
  while(left<right) {
    size_t mid=(left+right)/2;
    switch(Int::compare(val, values[mid])) {
    case LESS:
      right=mid;
      break;
    case GREATER:
      left=mid+1;
      break;
    case EQUAL:
      return targets[mid];
    }
  }
  ASS_EQ(left,right);
  ASS(left==length()-1 || val<=values[left]);

  if constexpr (!doInsert) {
    return targets[left];
  }
  if (val==values[left]) {
    return targets[left];
  }

  if (val>=values[left]) {
    left++;
  }
  targets.insert(targets.begin()+left,0);
  values.insert(values.begin()+left,val);
  return targets[left];
}

//////////////// Matcher ////////////////////

template<bool removing, bool checkRange>
bool CodeTree::Matcher<removing, checkRange>::execute()
{
  if(fresh) {
    fresh=false;
  }
  else {
    //we backtrack from what we found in the previous run
    if(!backtrack()) {
      return false;
    }
  }

  bool shouldBacktrack=false;
  for(;;) {
    if(op->alternative()) {
      if constexpr (removing) {
        btStack.push(BTPointRemoving(tp, op->alternative(), RemovingBase::firstsInBlocks->size()));
      } else {
        btStack.push(BTPoint(tp, op->alternative()));
      }
    }
    switch(op->_instruction()) {
      case SUCCESS_OR_FAIL:
        if(op->isFail()) {
          shouldBacktrack=true;
          break;
        }
        if constexpr (removing) {
          if (RemovingBase::matchingClauses) {
            //we can succeed only in certain depth and that will be handled separately
            shouldBacktrack=true;
          }
          else {
            //we are matching terms in a TermCodeTree
            return true;
          }
        } else {
          //yield successes only in the first round (we don't want to yield the
          //same thing for each query literal)
          if(curLInfo==firstLInfo) {
            return true;
          }
          else {
            shouldBacktrack=true;
          }
        }
        break;
      case LIT_END:
        if constexpr (removing) {
          ASS(RemovingBase::matchingClauses);
        }
        return true;
      case CHECK_GROUND_TERM:
        shouldBacktrack=!doCheckGroundTerm();
        break;
      case CHECK_FUN:
        shouldBacktrack=!doCheckFun();
        break;
      case ASSIGN_VAR:
        shouldBacktrack=!doAssignVar();
        break;
      case CHECK_VAR:
        shouldBacktrack=!doCheckVar();
        break;
      case SEARCH_STRUCT:
        if(doSearchStruct()) {
          //a new value of @b op is assigned, so restart the loop
          continue;
        }
        else {
          shouldBacktrack=true;
        }
        break;
      default: {
        ASSERTION_VIOLATION;
      }
    }
    if(shouldBacktrack) {
      if(!backtrack()) {
        return false;
      }
      shouldBacktrack=false;
    }
    else {
      //the SEARCH_STRUCT operation does not appear in CodeBlocks
      ASS(!op->isSearchStruct());
      //In each CodeBlock there is always either operation LIT_END or FAIL.
      //As we haven't encountered one yet, we may safely increase the
      //operation pointer
      op++;
    }
  }
}

template<bool removing, bool checkRange>
void CodeTree::Matcher<removing, checkRange>::init(const CodeTree& tree_, CodeOp* entry_, LitInfoOccupation occupied_, LitInfo* linfos_, size_t linfoCnt_, Stack<CodeOp*>* firstsInBlocks_)
{
  tree=&tree_;
  entry=entry_;
  occupied = occupied_;

  linfos=linfos_;
  linfoCnt=linfoCnt_;

  if constexpr (removing) {
    RemovingBase::firstsInBlocks=firstsInBlocks_;
    RemovingBase::initFIBDepth=RemovingBase::firstsInBlocks->size();
    RemovingBase::matchingClauses=tree->_clauseCodeTree;
  }

  fresh=true;
  _matched=false;
  curLInfo=0;
  firstLInfo=0;

  bindings.ensure(tree->_maxVarCnt);
  btStack.reset();
}

/**
 * Is called when we need to retrieve a new result.
 * It does not only backtrack to the next alternative to try,
 * but if there are no more alternatives, it goes back to the
 * entry point and starts evaluating new literal info (if there
 * is some left).
 */
template<bool removing, bool checkRange>
bool CodeTree::Matcher<removing, checkRange>::backtrack()
{
  if(btStack.isEmpty()) {
    curLInfo++;
    return prepareLiteral();
  }
  auto bp=btStack.pop();
  tp=bp.tp;
  op=bp.op;
  if constexpr (removing) {
    RemovingBase::firstsInBlocks->truncate(bp.fibDepth);
    RemovingBase::firstsInBlocks->push(op);
  }
  return true;
}

template<bool removing, bool checkRange>
bool CodeTree::Matcher<removing, checkRange>::prepareLiteral()
{
  if constexpr (removing) {
    RemovingBase::firstsInBlocks->truncate(RemovingBase::initFIBDepth);
  }
  while (curLInfo < linfoCnt && occupied.isOccupied(linfos[curLInfo])) {
    curLInfo++;
  }
  if(curLInfo>=linfoCnt) {
    return false;
  }
  ft=linfos[curLInfo].ft;
  tp=0;
  op=entry;
  return true;
}

template<bool removing, bool checkRange>
inline bool CodeTree::Matcher<removing, checkRange>::doAssignVar()
{
  ASS_EQ(op->_instruction(), ASSIGN_VAR);

  unsigned var=op->_arg();
  const FlatTerm::Entry* fte=&(*ft)[tp];
  if(fte->isVar()) {
    if constexpr (checkRange) {
      if (!RemovingBase::range.insert(fte->_number())) {
        return false;
      }
    }
    bindings[var]=TermList::var(fte->_number());
    tp++;
  }
  else {
    // in the removing case we are looking for variants
    // and they match only other variables into variables
    if constexpr (removing) {
      return false;
    }
    ASS(fte->isFun());
    fte++;
    ASS_EQ(fte->_tag(), FlatTerm::FUN_TERM_PTR);
    ASS(fte->_term());
    if (env.higherOrder()) {
      // When dealing with HOL we want to avoid binding to
      // any terms that contain loose DB indices.
      if (TermList(fte->_term()).containsLooseDBIndex()) {
        return false;
      }
    }
    bindings[var]=TermList(fte->_term());
    fte++;
    ASS_EQ(fte->_tag(), FlatTerm::FUN_RIGHT_OFS);
    tp+=fte->_number();
  }
  return true;
}

template<bool removing, bool checkRange>
inline bool CodeTree::Matcher<removing, checkRange>::doCheckVar()
{
  ASS_EQ(op->_instruction(), CHECK_VAR);

  unsigned var=op->_arg();
  const FlatTerm::Entry* fte=&(*ft)[tp];
  if (fte->isVar()) {
    if(bindings[var]!=TermList::var(fte->_number())) {
      return false;
    }
    tp++;
  }
  else {
    // in the removing case we are looking for variants
    // and they match only other variables into variables
    if constexpr (removing) {
      return false;
    }
    ASS(fte->isFun());
    fte++;
    ASS_EQ(fte->_tag(), FlatTerm::FUN_TERM_PTR);
    if(bindings[var]!=TermList(fte->_term())) {
      return false;
    }
    fte++;
    ASS_EQ(fte->_tag(), FlatTerm::FUN_RIGHT_OFS);
    tp+=fte->_number();
  }
  return true;
}

template<bool removing, bool checkRange>
inline bool CodeTree::Matcher<removing, checkRange>::doCheckFun()
{
  ASS_EQ(op->_instruction(), CHECK_FUN);

  unsigned functor=op->_arg();
  FlatTerm::Entry& fte=(*ft)[tp];
  if(!fte.isFun(functor)) {
    return false;
  }
  fte.expand();
  tp+=FlatTerm::FUNCTION_ENTRY_COUNT;
  return true;
}

template<bool removing, bool checkRange>
inline bool CodeTree::Matcher<removing, checkRange>::doCheckGroundTerm()
{
  ASS_EQ(op->_instruction(), CHECK_GROUND_TERM);

  const FlatTerm::Entry* fte=&(*ft)[tp];
  if(!fte->isFun()) {
    return false;
  }

  Term* trm=op->getTargetTerm();

  fte++;
  ASS_EQ(fte->_tag(), FlatTerm::FUN_TERM_PTR);
  ASS(fte->_term());
  if(trm!=fte->_term()) {
    return false;
  }
  fte++;
  ASS_EQ(fte->_tag(), FlatTerm::FUN_RIGHT_OFS);
  tp+=fte->_number();
  return true;
}

template<bool removing, bool checkRange>
inline bool CodeTree::Matcher<removing, checkRange>::doSearchStruct()
{
  ASS_EQ(op->_instruction(), SEARCH_STRUCT);

  const FlatTerm::Entry* fte=&(*ft)[tp];
  CodeOp* target=op->getSearchStruct()->getTargetOp(op, fte);
  if(!target) {
    return false;
  }
  op=target;
  if constexpr (removing) {
    RemovingBase::firstsInBlocks->push(op);
  }
  return true;
}

template struct CodeTree::Matcher<true, false>;
template struct CodeTree::Matcher<true, true>;
template struct CodeTree::Matcher<false, false>;

//////////////// auxiliary ////////////////////

CodeTree::~CodeTree()
{
  if(!isEmpty()) {
    destroySubtrees(_root);
    _root=nullptr;
  }
}

/**
 * Destroy the subtree @b st and all subtrees reachable from it.
 *
 * Operations that became unreachable (garbage) never own any
 * resources, so we can simply go over the whole arrays.
 */
void CodeTree::destroySubtrees(CodeTreeSubtree* st)
{
  Stack<CodeTreeSubtree*> todo;
  todo.push(st);
  while(todo.isNonEmpty()) {
    st=todo.pop();
    for(uint32_t i=0;i<st->size();i++) {
      CodeOp* op=&(*st)[i];
      if(op->isSearchStruct()) {
        op->getSearchStruct()->destroy();
        continue;
      }
      if(op->isLitEnd()) {
        ILStruct* ils=op->getILS();
        if(ils->next) {
          todo.push(ils->next);
        }
        delete ils;
        continue;
      }
      onCodeOpDestroying(op);
    }
    CodeTreeSubtree::deallocate(st);
  }
}


template<class Visitor>
void CodeTree::visitAllOps(Visitor visitor) const
{
  // operation, depth, and flag indicating whether the next functor is a predicate
  Stack<tuple<CodeOp*,unsigned,bool>> top_ops;
  // each top_op is either a first op of a block or a SEARCH_STRUCT

  if(!isEmpty()) { top_ops.emplace(getEntryPoint(),0,_clauseCodeTree); }

  while(top_ops.isNonEmpty()) {
    auto [top_op,depth,litStart] = top_ops.pop();

    if (top_op->isSearchStruct()) {
      visitor(top_op, depth, litStart);

      if(top_op->alternative()) {
        top_ops.emplace(top_op->alternative(),depth,litStart);
      }

      auto ss = top_op->getSearchStruct();
      for (size_t i = 0; i < ss->length(); i++) {
        if (ss->targets[i]!=0) { // zeros are allowed as targets (they are holes after removals)
          top_ops.emplace(top_op+ss->targets[i],depth+1,litStart);
        }
      }
      continue;
    }
    CodeOp* op=top_op;
    for(;;) {
      visitor(op, depth, litStart);
      if(op->alternative()) {
        top_ops.emplace(op->alternative(),depth,litStart);
      }
      if(op->isLitEnd()) {
        top_ops.emplace(op->literalContinuation(),depth+1,true);
        break;
      }
      if(op->isTerminal()) {
        break;
      }
      litStart=false;
      op++;
      depth++;
    }
  }
}


void CodeTree::printOps(std::ostream& out, const CodeTree& ct, const CodeTree::CodeStack& st) const
{
  bool litStart = _clauseCodeTree;
  for (const auto& op : st) {
    printOp(out, op, litStart);
    out << std::endl;
    litStart = op.isLitEnd();
  }
}

std::ostream& operator<<(std::ostream& out, const CodeTree& ct)
{
  ct.visitAllOps([&out,&ct](const CodeTree::CodeOp* op, unsigned depth, bool litStart) {
    for (unsigned i = 0; i < depth; i++) {
      out << "  ";
    }
    ct.printOp(out, *op, litStart);
    out << std::endl;
  });
  return out;
}

//////////////// insertion ////////////////////

template<bool forLits>
CodeTree::Compiler<forLits>::Compiler(CodeStack& code) : code(code), nextVarNum(0), nextGlobalVarNum(0) {}

template<bool forLits>
void CodeTree::Compiler<forLits>::nextLit()
{
  ASS(forLits);
  nextVarNum = 0;
  varMap.reset();
}

template<bool forLits>
void CodeTree::Compiler<forLits>::updateCodeTree(CodeTree* tree)
{
  //update the max. number of variables, if necessary
  if(nextGlobalVarNum>tree->_maxVarCnt) {
    tree->_maxVarCnt=nextGlobalVarNum;
  }
  if(nextVarNum>tree->_maxVarCnt) {
    tree->_maxVarCnt=nextVarNum;
  }
}

template<bool forLits>
void CodeTree::Compiler<forLits>::handleTerm(const Term* trm)
{
  ASS(!forLits || trm->isLiteral());

  static Stack<unsigned> globalCounterparts;
  globalCounterparts.reset();

  // TODO add LIT_END for the ground term check, but there is also
  // some problem with subsumption resolution with ground literals
  if (GROUND_TERM_CHECK && trm->ground() && !trm->isLiteral()) {
    code.push(CodeOp::getGroundTermCheck(trm));
    return;
  }

  if (trm->isLiteral()) {
    auto lit = static_cast<const Literal*>(trm);
    code.push(CodeOp::getTermOp(CHECK_FUN, lit->header()));
  } else {
    code.push(CodeOp::getTermOp(CHECK_FUN, trm->functor()));
  }

  handleSubterms(trm, globalCounterparts);

  // If literal is equality, we add a type argument
  // to properly match with two variable equalities.
  // This has to be done also in flat terms.
  if (trm->isLiteral()) {
    auto lit = static_cast<const Literal*>(trm);
    if (lit->isEquality()) {
      auto sort = SortHelper::getEqualityArgumentSort(lit);
      if (sort.isVar()) {
        handleVar(sort.var(), &globalCounterparts);
      } else {
        code.push(CodeOp::getTermOp(CHECK_FUN, sort.term()->functor()));
        handleSubterms(sort.term(), globalCounterparts);
      }
    }
  }

  if constexpr (forLits) {
    ASS(trm->isLiteral());  //LIT_END operation makes sense only for literals
    unsigned varCnt = nextVarNum;
    ASS_EQ(varCnt, globalCounterparts.size());
    auto ils = new ILStruct(static_cast<const Literal*>(trm), varCnt, globalCounterparts);
    code.push(CodeOp::getLitEnd(ils));
  }
}

template<bool forLits>
void CodeTree::Compiler<forLits>::handleVar(unsigned var, Stack<unsigned>* globalCounterparts)
{
  unsigned* varNumPtr;
  if (varMap.getValuePtr(var,varNumPtr)) {
    *varNumPtr = nextVarNum++;
    code.push(CodeOp::getTermOp(ASSIGN_VAR, *varNumPtr));

    if constexpr (forLits) {
      unsigned* globalVarNumPtr;
      if (globalVarMap.getValuePtr(var,globalVarNumPtr)) {
        *globalVarNumPtr = nextGlobalVarNum++;
      }
      globalCounterparts->push(*globalVarNumPtr);
    }
  } else {
    code.push(CodeOp::getTermOp(CHECK_VAR, *varNumPtr));
  }
}

template<bool forLits>
void CodeTree::Compiler<forLits>::handleSubterms(const Term* trm, Stack<unsigned>& globalCounterparts)
{
  SubtermIterator sti(trm);
  while (sti.hasNext()) {
    TermList s = sti.next();
    if (s.isVar()) {
      handleVar(s.var(), &globalCounterparts);
      continue;
    }
    ASS(s.isTerm());
    Term* t = s.term();

    if (GROUND_TERM_CHECK && t->ground()) {
      code.push(CodeOp::getGroundTermCheck(t));
      sti.right();
      continue;
    }

    code.push(CodeOp::getTermOp(CHECK_FUN, t->functor()));
  }
}

template struct CodeTree::Compiler<true>;
template struct CodeTree::Compiler<false>;

/**
 * Delete the ILStruct objects of the LIT_END operations among the first
 * @b cnt operations of @b code (these operations were matched with the
 * tree and are therefore not used) and empty the stack.
 */
void CodeTree::disposeCode(CodeStack& code, size_t cnt)
{
  ASS_LE(cnt,code.size());
  for(size_t i=0;i<cnt;i++) {
    if(code[i].isLitEnd()) {
      delete code[i].getILS();
    }
  }
  code.reset();
}

/**
 * Append to the subtree @b st (it is created if null) the operations of
 * @b code starting at @b start up to the first LIT_END (or SUCCESS). If the
 * LIT_END is immediately followed by the final SUCCESS, the SUCCESS is
 * appended as well, as the inline success of the LIT_END.
 *
 * This also sets the depth of each new ILStruct.
 *
 * Into @b firstIdx is assigned the index of the first appended op, into
 * @b lastILS the ILStruct of the appended LIT_END (or nullptr). Returns the
 * position in @b code after the appended operations.
 */
size_t CodeTree::appendSegment(CodeTreeSubtree*& st, CodeStack& code, size_t start, ILStruct* prev, uint32_t& firstIdx, ILStruct*& lastILS)
{
  size_t clen=code.size();
  size_t end=start;
  while(!code[end].isTerminal()) {
    end++;
    ASS_L(end,clen);
  }
  if(code[end].isLitEnd() && end+1<clen && code[end+1].isSuccess()) {
    end++;
  }
  end++;

  firstIdx=CodeTreeSubtree::append(st, end-start);
  CodeOp* tgt=&(*st)[firstIdx];
  lastILS=nullptr;
  for(size_t i=start;i<end;i++,tgt++) {
    CodeOp& op=code[i];
    ASS(!op.alternative()); //the ops should not have an alternative set yet
    if(op.isLitEnd()) {
      ILStruct* ils=op.getILS();
      ils->putIntoSequence(prev);
      ASS(!ils->next);
      lastILS=ils;
    }
    *tgt=op;
  }
  return end;
}

/**
 * Create subtrees for the literals of @b code from @b pos on.
 * The first of them becomes the next subtree of @b ils.
 */
void CodeTree::attachRest(CodeStack& code, size_t pos, ILStruct* ils)
{
  while(pos<code.size()) {
    ASS(ils);
    ASS(!ils->next);
    CodeTreeSubtree* st=nullptr;
    uint32_t firstIdx;
    ILStruct* last;
    pos=appendSegment(st, code, pos, ils, firstIdx, last);
    ASS_EQ(firstIdx,0);
    ils->next=st;
    ils=last;
  }
}

/**
 * Incorporate the code in @b code CodeStack into the tree, empty the
 * stack, and make sure all no longer necessary structures are freed.
 */
void CodeTree::incorporate(CodeStack& code)
{
  ASS(code.top().isSuccess());

  if(isEmpty()) {
    uint32_t firstIdx;
    ILStruct* last;
    size_t pos=appendSegment(_root, code, 0, nullptr, firstIdx, last);
    attachRest(code, pos, last);
    code.reset();
    return;
  }

  static const unsigned checkFunOpThreshold=5; //must be greater than 1 or it would cause loops
  static const unsigned checkGroundTermOpThreshold=3; //must be greater than 1 or it would cause loops

  size_t clen=code.length();
  //the pointer referring to the subtree we are in (pointers to subtrees
  //are stable while the subtrees themselves may be reallocated)
  CodeTreeSubtree** stRef=&_root;
  ILStruct* lastMatchedILS=0;
  //index of the current operation in the subtree
  uint32_t cur=0;
  //the new code is attached either as the alternative of the op at index
  //tailIdx, or to the SEARCH_STRUCT slot tailSlot of the SEARCH_STRUCT at tailIdx
  uint32_t tailIdx=0;
  int32_t* tailSlot=nullptr;
  size_t i=0;

  for(;;i++) {
    ASS_L(i,clen);
    uint32_t chainStart=cur;
    size_t checkFunOps=0;
    size_t checkGroundTermOps=0;
    for(;;) {
      CodeOp* treeOp=&(**stRef)[cur];
      if(treeOp->isSearchStruct()) {
        //handle the SEARCH_STRUCT
        int32_t* toPtr;
        if(treeOp->getSearchStruct()->getTargetOpPtr<true>(code[i], toPtr)) {
          if(!*toPtr) {
            tailIdx=cur;
            tailSlot=toPtr;
            goto matching_done;
          }
          cur+=*toPtr;
          continue;
        }
      } else if(!code[i].isSuccess() && code[i].equalsForOpMatching(*treeOp)) {
        //matched, go to the next compiled instruction
        //(SUCCESS operations are never shared, so we insert an
        //entry multiple times if needed, since later we may be
        //removing it multiple times as well)
        break;
      }

      if(treeOp->alternative()) {
        //try alternative if there is some
        cur+=treeOp->alternativeOffset();
      } else {
        //matching failed, we'll add the new branch here
        tailIdx=cur;
        goto matching_done;
      }
      treeOp=&(**stRef)[cur];

      if(treeOp->isCheckFun()) {
        checkFunOps++;
        //if there were too many CHECK_FUN alternative operations, put them
        //into a SEARCH_STRUCT
        if(checkFunOps>checkFunOpThreshold) {
          //we put CHECK_FUN ops into the SEARCH_STRUCT op, and
          //restart with the chain
          compressCheckOps<SearchStruct::FN_STRUCT>(*stRef, chainStart);
          cur=chainStart;
          checkFunOps=0;
          checkGroundTermOps=0;
          continue;
        }
      }

      if(treeOp->isCheckGroundTerm()) {
        checkGroundTermOps++;
        //if there were too many CHECK_GROUND_TERM alternative operations, put them
        //into a SEARCH_STRUCT
        if(checkGroundTermOps>checkGroundTermOpThreshold) {
          //we put CHECK_GROUND_TERM ops into the SEARCH_STRUCT op, and
          //restart with the chain
          compressCheckOps<SearchStruct::GROUND_TERM_STRUCT>(*stRef, chainStart);
          cur=chainStart;
          checkFunOps=0;
          checkGroundTermOps=0;
          continue;
        }
      }
    } // for(;;)

    CodeOp* treeOp=&(**stRef)[cur];
    if(treeOp->isLitEnd()) {
      lastMatchedILS=treeOp->getILS();
      if (!lastMatchedILS->next) {
        // A second branch needs an ordinary subtree. Preserve the old SUCCESS
        // as its root so alternatives retain their original execution order.
        ASS((treeOp+1)->isSuccess());
        CodeTreeSubtree::append(lastMatchedILS->next, 1);
        *lastMatchedILS->next->root() = treeOp[1];
        treeOp[1].makeFail();
        ++(*stRef)->_garbage;
      }
      //continue in the subtree of the next literal
      stRef=&lastMatchedILS->next;
      cur=0;
      continue;
    }

    //we can safely do increase because as long as we match and something
    //remains in the @b code stack, we aren't at the end of the code block
    ASS(!treeOp->isTerminal());
    ASS(!treeOp->isSearchStruct());
    cur++;
  }

matching_done:
  ASS_L(i,clen);
  RSTAT_MCTR_INC("alt split literal", lastMatchedILS ? (lastMatchedILS->depth+1) : 0);
  {
    uint32_t firstIdx;
    ILStruct* last;
    size_t pos=appendSegment(*stRef, code, i, lastMatchedILS, firstIdx, last);
    if(tailSlot) {
      //the SearchStruct is not stored in the subtree, so tailSlot is still valid
      *tailSlot=static_cast<int32_t>(firstIdx)-static_cast<int32_t>(tailIdx);
    } else {
      CodeOp* tail=&(**stRef)[tailIdx];
      tail->setAlternative(&(**stRef)[firstIdx]);
    }
    if(pos<clen) {
      attachRest(code, pos, last);
    }
  }
  maybeCompact(*stRef);
  disposeCode(code, i);
}

template<CodeTree::SearchStruct::Kind k>
void CodeTree::compressCheckOps(CodeTreeSubtree*& st, uint32_t chainStartIdx)
{
  ASS((*st)[chainStartIdx].alternative());

  static Stack<uint32_t> toDo;
  static Stack<uint32_t> chfOps;
  static Stack<uint32_t> otherOps;
  toDo.reset();
  chfOps.reset();
  otherOps.reset();

  toDo.push(chainStartIdx+(*st)[chainStartIdx].alternativeOffset());
  while (toDo.isNonEmpty()) {
    uint32_t idx = toDo.pop();
    CodeOp* op = &(*st)[idx];
    if (op->alternative()) {
      toDo.push(idx+op->alternativeOffset());
    }
    bool ofKind;
    if constexpr (k == SearchStruct::FN_STRUCT) {
      ofKind = op->isCheckFun();
    } else {
      ofKind = op->isCheckGroundTerm();
    }

    if (ofKind) {
      chfOps.push(idx);
    } else if (op->isSearchStruct()) {
      auto ss = op->getSearchStruct();
      if (ss->kind == k) {
        for (size_t i = 0; i < ss->length(); i++) {
          if (ss->targets[i]) {
            toDo.push(idx+ss->targets[i]);
          }
        }
        ss->destroy();
        //the op becomes garbage
        op->makeFail();
        op->setAlternativeOffset(0);
        st->_garbage++;
      } else {
        otherOps.push(idx);
      }
    } else {
      otherOps.push(idx);
    }
  }

  ASS_G(chfOps.size(),1);
  size_t slen=chfOps.size();
  auto res=new SearchStructImpl<k>(slen);

  CodeOp* ops=st->ops();
  sort(chfOps.begin(), chfOps.end(), [ops](uint32_t i1, uint32_t i2) {
    if constexpr (k==SearchStruct::FN_STRUCT) {
      return ops[i1]._arg() < ops[i2]._arg();
    } else {
      return ops[i1].getTargetTerm() < ops[i2].getTargetTerm();
    }
  });

  //the SEARCH_STRUCT op is put at the end of the subtree
  uint32_t ssIdx=CodeTreeSubtree::append(st,1);
  ops=st->ops();
  ops[ssIdx]=CodeOp::getSearchStructOp(res);

  for(size_t i=0;i<slen;i++) {
    CodeOp& chf=ops[chfOps[i]];
    if constexpr (k==SearchStruct::FN_STRUCT) {
      ASS(chf.isCheckFun());
      res->values.push_back(chf._arg());
    } else {
      ASS(chf.isCheckGroundTerm());
      res->values.push_back(chf.getTargetTerm());
    }
    res->targets.push_back(static_cast<int32_t>(chfOps[i])-static_cast<int32_t>(ssIdx));
    chf.setAlternativeOffset(0);
  }

  CodeOp* op=&ops[ssIdx];
  ops[chainStartIdx].setAlternative(op);
  while(otherOps.isNonEmpty()) {
    CodeOp* next=&ops[otherOps.pop()];
    op->setAlternative(next);
    op=next;
  }
  op->setAlternativeOffset(0);
}

void CodeTree::maybeCompact(CodeTreeSubtree*& st)
{
  if(!st) {
    return;
  }
  // Compact when at least half of the subtree is garbage, and also each
  // time the subtree doubled its size by appends, to restore locality of
  // the layout. Both are amortized constant per operation.
  if(st->_garbage*2>st->_size ||
     (st->_size>=32 && st->_size>=2*st->_compactedSize)) {
    compact(st);
  }
}

/**
 * Re-lay out the reachable operations of the subtree in the depth-first
 * order of code blocks, so that each block is followed by its alternatives,
 * and drop the garbage.
 */
void CodeTree::compact(CodeTreeSubtree*& st)
{
  static constexpr uint32_t NONE=std::numeric_limits<uint32_t>::max();

  uint32_t n=st->size();
  CodeOp* ops=st->ops();

  std::vector<uint32_t> map(n,NONE);
  static Stack<uint32_t> order;
  static Stack<uint32_t> work;
  static Stack<uint32_t> alts;
  order.reset();
  work.reset();

  //a failing root can be replaced by its alternative (unless it is a SEARCH_STRUCT,
  //which cannot be the first operation of the subtree)
  uint32_t rootIdx=0;
  while(ops[rootIdx].isFail() && ops[rootIdx].alternative() && !ops[rootIdx].alternative()->isSearchStruct()) {
    rootIdx+=ops[rootIdx].alternativeOffset();
  }
  work.push(rootIdx);

  //push the collected alternatives so that the first one is processed first
  auto pushAlts = [&]() {
    while(alts.isNonEmpty()) {
      work.push(alts.pop());
    }
  };

  while(work.isNonEmpty()) {
    uint32_t b=work.pop();
    ASS_EQ(map[b],NONE);
    alts.reset();
    if(ops[b].isSearchStruct()) {
      map[b]=order.size();
      order.push(b);
      for(auto t : ops[b].getSearchStruct()->targets) {
        if(t) {
          alts.push(b+t);
        }
      }
      if(ops[b].alternative()) {
        alts.push(b+ops[b].alternativeOffset());
      }
      pushAlts();
      continue;
    }
    uint32_t kIdx=b;
    for(;;) {
      map[kIdx]=order.size();
      order.push(kIdx);
      if(ops[kIdx].alternative()) {
        alts.push(kIdx+ops[kIdx].alternativeOffset());
      }
      if(ops[kIdx].isTerminal()) {
        break;
      }
      kIdx++;
    }
    if(ops[kIdx].isLitEnd() && !ops[kIdx].getILS()->next) {
      ASS(ops[kIdx+1].isSuccess());
      order.push(kIdx+1);
    }
    pushAlts();
  }

  uint32_t m=order.size();
  CodeTreeSubtree* nst=CodeTreeSubtree::create(std::max(m,1u));
  nst->_size=m;
  nst->_compactedSize=m;
  CodeOp* nops=nst->ops();
  for(uint32_t i=0;i<m;i++) {
    uint32_t o=order[i];
    nops[i]=ops[o];
    if(ops[o].alternativeOffset()) {
      uint32_t t=o+ops[o].alternativeOffset();
      ASS_NEQ(map[t],NONE);
      nops[i].setAlternativeOffset(static_cast<int32_t>(map[t])-static_cast<int32_t>(i));
    }
    if(ops[o].isSearchStruct()) {
      for(auto& t : ops[o].getSearchStruct()->targets) {
        if(t) {
          ASS_NEQ(map[o+t],NONE);
          t=static_cast<int32_t>(map[o+t])-static_cast<int32_t>(i);
        }
      }
    }
  }
  CodeTreeSubtree::deallocate(st);
  st=nst;
}

//////////// removal //////////////

/**
 * Turn @b op into FAIL (keeping its alternative), releasing what it owns.
 */
void CodeTree::killOp(CodeOp* op)
{
  ASS(!op->isSearchStruct());
  if(op->isLitEnd()) {
    ASS(!op->getILS()->next);
    delete op->getILS();
  }
  op->makeFail();
}

/**
 * Remove the terminal operation @b op, which does not lead to any stored
 * entry anymore, and unlink the code that becomes unnecessary because of it.
 *
 * The first operations of the code blocks on the path to @b op within the
 * subtree @b st are on the @b firstsInBlocks stack above the position
 * @b fibStart (the top one is the first operation of the block containing @b op).
 *
 * Return true iff the whole subtree @b st became unreachable (it is then
 * up to the caller to deallocate it).
 */
bool CodeTree::removeDeadOp(Stack<CodeOp*>& firstsInBlocks, size_t fibStart, CodeOp* op, CodeTreeSubtree* st)
{
  ASS(op->isTerminal());
  killOp(op);

  ASS_G(firstsInBlocks.size(),fibStart);
  CodeOp* firstOp=firstsInBlocks.pop();
  //the last operation of the part of the block that is dead
  CodeOp* deadEnd=op;
  for(;;) {
    //firstOp is the first op of a block that contains op
    ASS(!firstOp->isSearchStruct());
    ASS_LE(firstOp, op);

    while(op>firstOp && !op->alternative()) { ASS(!op->isSuccess()); op--; }

    if(op!=firstOp) {
      ASS(op->alternative());
      //we only change the instruction, the alternative must remain unchanged
      killOp(op);
      st->_garbage+=deadEnd-op;
      return false;
    }
    CodeOp* alt=firstOp->alternative();

    if(firstsInBlocks.size()==fibStart) {
      //the block is at the root of the subtree, which must stay at its place
      ASS_EQ(firstOp,st->root());
      if(alt) {
        killOp(firstOp);
        st->_garbage+=deadEnd-firstOp;
        return false;
      }
      return true;
    }

    //the whole block is unreachable now
    st->_garbage+=deadEnd-firstOp+1;
    CodeOp firstOpCopy=*firstOp;

    //first operation in the block that points to the current one
    CodeOp* prevFirstOp=firstsInBlocks.pop();

    if(prevFirstOp->isSearchStruct()) {
      if(prevFirstOp->alternative()==firstOp) {
        //firstOp was an alternative to the SearchStruct
        prevFirstOp->setAlternative(alt);
        return false;
      }
      auto ss=prevFirstOp->getSearchStruct();
      int32_t* tgtPtr;
      ALWAYS(ss->getTargetOpPtr<false>(firstOpCopy, tgtPtr));
      ASS_EQ(prevFirstOp+*tgtPtr, firstOp);
      *tgtPtr=alt ? static_cast<int32_t>(alt-prevFirstOp) : 0;
      if(alt) {
        ASS( (ss->kind==SearchStruct::FN_STRUCT && alt->isCheckFun()) ||
            (ss->kind==SearchStruct::GROUND_TERM_STRUCT && alt->isCheckGroundTerm()) );
        return false;
      }
      for(size_t i=0; i<ss->length(); i++) {
        if(ss->targets[i]!=0) {
          //the SearchStruct still contains something, so we won't delete it
          //TODO: we might want to compress the SearchStruct, if there are too many zeroes
          return false;
        }
      }

      //if we're at this point, the SEARCH_STRUCT will be deleted
      firstOp=prevFirstOp;
      alt=firstOp->alternative();
      ss->destroy();
      firstOp->makeFail();
      firstOp->setAlternativeOffset(0);
      st->_garbage++;

      //now let's continue as if there wasn't any SEARCH_STRUCT operation:)

      //the SEARCH_STRUCT is never the first operation in a subtree
      ASS_G(firstsInBlocks.size(),fibStart);
      prevFirstOp=firstsInBlocks.pop();
      //there never are two nested SEARCH_STRUCT operations
      ASS(!prevFirstOp->isSearchStruct());
    }

    //operation that points to the current block
    CodeOp* pointingOp=prevFirstOp;
    while(pointingOp->alternative()!=firstOp) {
      ASS(!pointingOp->isTerminal());
      pointingOp++;
    }
    pointingOp->setAlternative(alt);

    //check whether the rest of the block after pointingOp is dead
    CodeOp* prevOp=pointingOp;
    if(!prevOp->isTerminal()) {
      prevOp++;
      for(;;) {
        if(prevOp->alternative()) {
          //there is an operation after the pointingOp that cannot be lost
          return false;
        }
        if(prevOp->isTerminal()) {
          break;
        }
        prevOp++;
      }
    }
    if(!prevOp->isFail()) {
      //a SUCCESS or a LIT_END (all LIT_ENDs in the tree lead somewhere)
      return false;
    }

    deadEnd=prevOp;
    firstOp=prevFirstOp;
    op=pointingOp;
  }
}

void CodeTree::optimizeMemoryAfterRemoval(Stack<CodeOp*>* firstsInBlocks, CodeOp* removedOp)
{
  ASS(removedOp->isFail());
  LOG_OP("Code tree removal memory optimization");
  LOG_OP("firstsInBlocks->size()="<<firstsInBlocks->size());

  if(removeDeadOp(*firstsInBlocks, 0, removedOp, _root)) {
    destroySubtrees(_root);
    _root=nullptr;
    return;
  }
  maybeCompact(_root);
}



//////////////// retrieval ////////////////////

void CodeTree::incTimeStamp()
{
  _curTimeStamp++;
  if(!_curTimeStamp) {
    //handle overflow
    NOT_IMPLEMENTED;
  }
}

}
