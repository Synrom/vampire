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
 * Ablation-study helper: calls a ClauseMatcher over @b tree for @b cl
 * exactly once, returning whether that single call found a match. Used to
 * compare every approach's matching behaviour against each other and
 * against the current implementation, with every approach's
 * ClauseMatcher::next called exactly once per
 * CodeTreeForwardSubsumptionAndResolution::perform call - never drained -
 * so op-count and timing measurements stay comparable across approaches.
 */
template<class Tree>
static bool ablationCallMatcherOnce(Tree* tree, Clause* cl, bool subsumptionResolution)
{
  if (tree->isEmpty()) {
    return false;
  }
  static typename Tree::ClauseMatcher matcher;
  matcher.init(tree, cl, subsumptionResolution);
  int resolved;
  bool found = matcher.next(resolved) != nullptr;
  matcher.reset();
  return found;
}

bool CodeTreeForwardSubsumptionAndResolution::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  if (_ct->isEmpty()) {
    return false;
  }

  // Ablation study: call every OTHER approach's matcher exactly once over
  // the same query clause, purely for timing (each ClauseMatcher::next has
  // its own named TIME_TRACE) and for the cross-approach invariant checked
  // below. The current implementation's own matcher is exercised by the
  // single call below instead of being called a second time here.
  bool masterFoundMatch = ablationCallMatcherOnce(_index->getMasterTree(), cl, _subsumptionResolution);

  static typename ClauseCodeTree::ClauseMatcher cm;

  cm.init(_ct, cl, _subsumptionResolution);

  int resolvedQueryLit;
  Clause* premise = cm.next(resolvedQueryLit);
  bool currentFoundMatch = premise != nullptr;
  cm.reset();

  // Invariant: the current implementation must never miss a match that master finds.
  ASS(!masterFoundMatch || currentFoundMatch);

  if (!premise) {
    return false;
  }

  if (resolvedQueryLit == -1) {
    ASS(satSubs.checkSubsumption(premise, cl));
    premises = pvi(getSingletonIterator(premise));
    env.statistics->forwardSubsumed++;
    return true;
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
  return true;
}

} // namespace Inferences
