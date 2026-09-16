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
 * @file CodeTreeInterfaces.hpp
 * Defines classes of indexing structures that use code trees.
 */

#ifndef __CodeTreeInterfaces__
#define __CodeTreeInterfaces__

#include "Forwards.hpp"
#include "Kernel/SubstHelper.hpp"
#include "Kernel/Renaming.hpp"

#include "LiteralCodeTree.hpp"
#include "TermCodeTree.hpp"
#include "ClauseCodeTree.hpp"

#include "master/ClauseCodeTree.hpp"
#include "ordering/ClauseCodeTree.hpp"
#include "simplify-canEnterLiteral-first/ClauseCodeTree.hpp"
#include "simplify-canEnterLiteral-second/ClauseCodeTree.hpp"
#include "remove-touchedSlots/ClauseCodeTree.hpp"
#include "save-bindings-directly/ClauseCodeTree.hpp"
#include "ilstruct-simplification/ClauseCodeTree.hpp"

#include "Index.hpp"

#include <algorithm>
#include <vector>

namespace Indexing
{

using namespace Kernel;
using namespace Lib;

template<class Data>
class GenSubstitution
  : public SubstApplicator
{
public:
  GenSubstitution(const CodeTree::BindingArray& bindings, const Renaming& resultNormalizer)
  : _bindings(bindings), _resultNormalizer(resultNormalizer) {}

  TermList apply(unsigned var) const override {
    if constexpr (!is_indexed_data_normalized<Data>::value) {
      ASS(_resultNormalizer.contains(var));
      var = _resultNormalizer.get(var);
    }
    return _bindings[var];
  }

  template<typename Val> Val apply(Val v) const
  { return SubstHelper::apply(v, *this); }

private:
  const CodeTree::BindingArray& _bindings;
  const Renaming& _resultNormalizer;
};

template<typename Data>
using GenSubstitutionQR = QueryRes<const GenSubstitution<Data>&, Data>;

template<typename Data, typename Matcher>
class ResultIterator
  : public IteratorCore<GenSubstitutionQR<Data>>
{
public:
  template<typename ...Args>
  ResultIterator(const CodeTree& tree, Args... args)
  : _subst(_matcher->bindings, *_resultNormalizer)
  {
    _matcher->init(tree, args...);
  }

  USE_ALLOCATOR(ResultIterator);

  bool hasNext() override
  {
    if(_found) {
      return true;
    }
    if(_finished) {
      return false;
    }
    _found = _matcher->next();
    if(!_found) {
      _finished=true;
    }
    return _found;
  }

  GenSubstitutionQR<Data> next() override
  {
    ASS(_found);
    if constexpr (!is_indexed_data_normalized<Data>::value) {
      _resultNormalizer->reset();
      _resultNormalizer->normalizeVariables(_found->key());
    }
    auto out = GenSubstitutionQR<Data>(_subst, _found);
    _found = nullptr;
    return out;
  }
private:
  Recycled<Renaming> _resultNormalizer;
  Data* _found = nullptr;
  bool _finished = false;
  Recycled<Matcher> _matcher;
  GenSubstitution<Data> _subst;
};

/**
 * Term indexing structure using code trees to retrieve generalizations
 */
template<class Data>
class CodeTreeTIS
{
public:
  /* INFO: we ignore unifying the sort of the keys here */
  void handle(Data data, bool insert)
  {
    if (insert) {
      auto ti = new Data(std::move(data));
      _ct.insert(ti);
    } else {
      _ct.remove(data);
    }
  }

  auto getGeneralizations(TypedTermList t) const {
    if(_ct.isEmpty()) {
      return VirtualIterator<GenSubstitutionQR<Data>>::getEmpty();
    }

    return vi( new ResultIterator<Data, typename TermCodeTree<Data>::TermMatcher>(_ct, t) );
  }

private:
  TermCodeTree<Data> _ct;
};

/**
 * Literal indexing structure using code trees to retrieve generalizations
 */
template<class Data>
class CodeTreeLIS
{
public:
  /* INFO: we ignore unifying the sort of the keys here */
  void handle(Data data, bool insert)
  {
    if (insert) {
      auto ti = new Data(std::move(data));
      _ct.insert(ti);
    } else {
      _ct.remove(data);
    }
  }

  auto getGeneralizations(Literal* lit, bool complementary) const
  {
    if(_ct.isEmpty()) {
      return VirtualIterator<GenSubstitutionQR<Data>>::getEmpty();
    }
    return vi( new ResultIterator<Data, typename LiteralCodeTree<Data>::LiteralMatcher>(_ct, lit, complementary) );
  }

  friend std::ostream& operator<<(std::ostream& out, Output::Multiline<CodeTreeLIS<Data>> const& self)
  { return out << self.self._ct; }

private:
  LiteralCodeTree<Data> _ct;
};

/**
 * Ablation-study helper: checks structural invariants of a ClauseCodeTree.
 * Ported from the InvariantTester in UnitTests/tClauseCodeTrees.cpp; only
 * the checks the ablation study actually calls are kept here.
 */
struct InvariantTester {
  ClauseCodeTree& tree;
  explicit InvariantTester(ClauseCodeTree& tree) : tree(tree) {}

  bool checkAllClausesAppear(std::vector<Clause*> expected) {
    std::vector<Clause*> actual;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isSuccess()) { actual.push_back(op->getSuccessResult<Clause>()); }
    });
    std::sort(actual.begin(), actual.end(), std::less<Clause*>{});
    std::sort(expected.begin(), expected.end(), std::less<Clause*>{});
    ASS(actual == expected);
    return true;
  }

  bool checkNoConsecutiveNextOps() {
    tree.visitAllOps([](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isNext()) { ASS(!op[1].isNext()); }
    });
    return true;
  }

  bool checkILSDepths() {
    tree.visitAllOps([](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isLitEnd()) {
        auto ils = op->getILS();
        ASS_EQ(ils->depth, ils->previous ? ils->previous->depth + 1 : 0);
        ASS_EQ(ils->hasContinuations, ils->continuations.isNonEmpty());
        for (const auto& cont : ils->continuations) {
          ASS(cont.entry);
          if (ils->previous) { ASS_L(cont.slot, ils->previous->successorSlotCount); }
        }
      }
    });
    return true;
  }
};

class CodeTreeSubsumptionIndex
: public Index
{
public:
  CodeTreeSubsumptionIndex(SaturationAlgorithm&) {}
  ClauseCodeTree* getClauseCodeTree() { return &_ct; }

  Ablation::Master::ClauseCodeTree* getMasterTree() { return &_ctMaster; }
  Ablation::Ordering::ClauseCodeTree* getOrderingTree() { return &_ctOrdering; }
  Ablation::SimplifyCanEnterFirst::ClauseCodeTree* getSimplifyCanEnterFirstTree() { return &_ctSimplifyCanEnterFirst; }
  Ablation::SimplifyCanEnterSecond::ClauseCodeTree* getSimplifyCanEnterSecondTree() { return &_ctSimplifyCanEnterSecond; }
  Ablation::RemoveTouchedSlots::ClauseCodeTree* getRemoveTouchedSlotsTree() { return &_ctRemoveTouchedSlots; }
  Ablation::SaveBindingsDirectly::ClauseCodeTree* getSaveBindingsDirectlyTree() { return &_ctSaveBindingsDirectly; }
  Ablation::ILStructSimplification::ClauseCodeTree* getILStructSimplificationTree() { return &_ctILStructSimplification; }

protected:
  void handleClause(Clause* c, bool adding) override {
    TIME_TRACE("codetree subsumption index maintenance");

    if(adding) {
      _ct.insert(c);
      _ctMaster.insert(c);
      _ctOrdering.insert(c);
      _ctSimplifyCanEnterFirst.insert(c);
      _ctSimplifyCanEnterSecond.insert(c);
      _ctRemoveTouchedSlots.insert(c);
      _ctSaveBindingsDirectly.insert(c);
      _ctILStructSimplification.insert(c);
      _trackedClauses.push_back(c);
    }
    else {
      _ct.remove(c);
      _ctMaster.remove(c);
      _ctOrdering.remove(c);
      _ctSimplifyCanEnterFirst.remove(c);
      _ctSimplifyCanEnterSecond.remove(c);
      _ctRemoveTouchedSlots.remove(c);
      _ctSaveBindingsDirectly.remove(c);
      _ctILStructSimplification.remove(c);
      auto it = std::find(_trackedClauses.begin(), _trackedClauses.end(), c);
      ASS(it != _trackedClauses.end());
      _trackedClauses.erase(it);
    }

    InvariantTester tester(_ct);
    ASS(tester.checkAllClausesAppear(_trackedClauses));
    ASS(tester.checkNoConsecutiveNextOps());
    ASS(tester.checkILSDepths());
  }
private:
  ClauseCodeTree _ct;
  Ablation::Master::ClauseCodeTree _ctMaster;
  Ablation::Ordering::ClauseCodeTree _ctOrdering;
  Ablation::SimplifyCanEnterFirst::ClauseCodeTree _ctSimplifyCanEnterFirst;
  Ablation::SimplifyCanEnterSecond::ClauseCodeTree _ctSimplifyCanEnterSecond;
  Ablation::RemoveTouchedSlots::ClauseCodeTree _ctRemoveTouchedSlots;
  Ablation::SaveBindingsDirectly::ClauseCodeTree _ctSaveBindingsDirectly;
  Ablation::ILStructSimplification::ClauseCodeTree _ctILStructSimplification;
  std::vector<Clause*> _trackedClauses;
};

};
#endif /*__CodeTreeInterfaces__*/
