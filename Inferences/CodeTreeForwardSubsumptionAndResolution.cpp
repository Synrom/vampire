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

#include "Saturation/SaturationAlgorithm.hpp"

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

namespace Inferences {

template<bool higherOrder>
CodeTreeForwardSubsumptionAndResolution<higherOrder>::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex<higherOrder>>()),
    _ct(_index->getClauseCodeTree()),
    _optimizedCt(_index->getOptimizedClauseCodeTree())
{}

template<bool higherOrder>
bool CodeTreeForwardSubsumptionAndResolution<higherOrder>::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  ASS_EQ(_ct->isEmpty(), _optimizedCt->isEmpty());
  if (_ct->isEmpty()) {
    return false;
  }

  static typename ClauseCodeTree<higherOrder>::ClauseMatcher oldCm;
  static typename OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher optimizedCm;

  oldCm.init(_ct, cl, _subsumptionResolution);
  optimizedCm.init(_optimizedCt, cl, _subsumptionResolution);

  Clause* premise = 0;
  int resolvedQueryLit = -1;

  static Stack<Clause*> oldResults;
  static Stack<Clause*> optimizedResults;
  oldResults.reset();
  optimizedResults.reset();

  //std::cout << "Run on " << cl->toReproducerString() << std::endl;

  premise = oldCm.next(resolvedQueryLit);
  premise = optimizedCm.next(resolvedQueryLit);

  /*
  if (optimizedCm.countCheckCandidate > oldCm.countCheckCandidate) {
    std::cout << "Optimized check Candidate count " << optimizedCm.countCheckCandidate << std::endl;
    std::cout << "Normal check Candidate count " << oldCm.countCheckCandidate << std::endl;
    //ASS(optimizedCm.countCheckCandidate <= oldCm.countCheckCandidate);
  }
  */
  //ASS(optimizedCm.countCanEnterLiteral == oldCm.countCanEnterLiteral);

  /*
  {
    Clause* c;
    int rql;
    while ((c = oldCm.next(rql))) {
      if (!premise) {
        premise = c;
        resolvedQueryLit = rql;
      }
      oldResults.push(c);
    }
    while ((c = optimizedCm.next(rql))) {
      optimizedResults.push(c);
    }
  }
  for (Clause* c : oldResults) {
    bool found = false;
    for (Clause* opt : optimizedResults) {
      if (opt == c) {
        found = true;
        break;
      }
    }
    ASS_REP(found, c->toString());
  }
  */

  oldCm.reset();
  optimizedCm.reset();

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

template class CodeTreeForwardSubsumptionAndResolution<false>;
template class CodeTreeForwardSubsumptionAndResolution<true>;

} // namespace Inferences
