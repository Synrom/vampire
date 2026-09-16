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
 * @file CodeTreeForwardSubsumptionAndResolution.cpp
 * Implements class CodeTreeForwardSubsumptionAndResolution.
 */

#include "Lib/Environment.hpp"
#include "Lib/Random.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

namespace Inferences {

CodeTreeForwardSubsumptionAndResolution::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex>()),
    _ct(_index->getClauseCodeTree())
{}

/**
 * Ablation-study helper: fully drains a ClauseMatcher over @b tree for @b cl,
 * returning whether it found any match at all. Used to compare every
 * approach's matching behaviour against each other and against the current
 * implementation.
 */
template<class Tree>
static bool ablationDrainMatcher(Tree* tree, Clause* cl, bool subsumptionResolution)
{
  if (tree->isEmpty()) {
    return false;
  }
  static typename Tree::ClauseMatcher matcher;
  matcher.init(tree, cl, subsumptionResolution);
  bool found = false;
  int resolved;
  while (matcher.next(resolved)) {
    found = true;
  }
  matcher.reset();
  return found;
}

bool CodeTreeForwardSubsumptionAndResolution::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  if (_ct->isEmpty()) {
    return false;
  }

  // under randomized simplifications, each subsumption resolution match is with this
  // probability dropped, giving the next match (possibly a proper subsumption, which
  // is never leaky) a chance instead (to be tuned)
  constexpr double RSI_SKIP_PROB = 0.02;
  bool rsi = env.options->randomizedSimplifications();

  // Ablation study: run every OTHER approach's matcher over the same query
  // clause, purely for timing (each ClauseMatcher::next has its own named
  // TIME_TRACE) and for the cross-approach invariant checked below. The
  // current implementation's own matcher is exercised by the real loop
  // below instead of being drained a second time here, so its
  // ClauseMatcher::next is only ever called once per candidate.
  bool masterFoundMatch = ablationDrainMatcher(_index->getMasterTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getOrderingTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getSimplifyCanEnterFirstTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getSimplifyCanEnterSecondTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getRemoveTouchedSlotsTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getSaveBindingsDirectlyTree(), cl, _subsumptionResolution);
  ablationDrainMatcher(_index->getILStructSimplificationTree(), cl, _subsumptionResolution);

  static typename ClauseCodeTree::ClauseMatcher cm;

  cm.init(_ct, cl, _subsumptionResolution);

  Clause* premise;
  int resolvedQueryLit;
  bool currentFoundMatch = false;

  while ((premise = cm.next(resolvedQueryLit))) {
    currentFoundMatch = true;
    if (resolvedQueryLit == -1) {
      ASS(satSubs.checkSubsumption(premise, cl));
      premises = pvi(getSingletonIterator(premise));
      env.statistics->forwardSubsumed++;
      cm.reset();
      // Invariant: the current implementation must never miss a match that master finds.
      ASS(!masterFoundMatch || currentFoundMatch);
      return true;
    }
    if (rsi && Random::getDouble(0.0,1.0) < RSI_SKIP_PROB) {
      continue; // drop this candidate; the next match gets a chance
    }
    ASS(satSubs.checkSubsumptionResolutionWithLiteral(premise, cl, resolvedQueryLit));

    LiteralStack res;
    for (unsigned i = 0; i < cl->length(); i++) {
      if (i == (unsigned)resolvedQueryLit) {
        continue;
      }
      res.push((*cl)[i]);
    }
    replacement = Clause::fromStack(res, SimplifyingInference2(InferenceRule::FORWARD_SUBSUMPTION_RESOLUTION, cl, premise));
    if(env.options->proofExtra() == Options::ProofExtra::FULL)
      env.proofExtra.insert(replacement, new LiteralInferenceExtra((*cl)[resolvedQueryLit]));
    premises = pvi(getSingletonIterator(premise));
    cm.reset();
    // Invariant: the current implementation must never miss a match that master finds.
    ASS(!masterFoundMatch || currentFoundMatch);
    return true;
  }

  cm.reset();
  // Invariant: the current implementation must never miss a match that master finds.
  ASS(!masterFoundMatch || currentFoundMatch);
  return false;
}

} // namespace Inferences
