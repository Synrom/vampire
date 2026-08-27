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
  ASS_EQ(_optimizedCt->isEmpty(), _ct->isEmpty());
  if (_optimizedCt->isEmpty()) {
    return false;
  }

  //static typename ClauseCodeTree<higherOrder>::ClauseMatcher oldCm;
  static typename OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher optimizedCm;

  //oldCm.init(_ct, cl, _subsumptionResolution);
  optimizedCm.init(_optimizedCt, cl, _subsumptionResolution);

  Clause* premise = 0;
  int resolvedQueryLit = -1;

  std::cout << "Run on " << cl->toReproducerString() << std::endl;
  premise = optimizedCm.next(resolvedQueryLit);
  optimizedCm.reset();


  /*
  static Stack<std::pair<Clause*, int>> oldResults;
  static Stack<std::pair<Clause*, int>> optimizedResults;
  oldResults.reset();
  optimizedResults.reset();

  {
    Clause* c;
    int rql;
    while ((c = oldCm.next(rql))) {
      oldResults.push(std::make_pair(c, rql));
    }
    while ((c = optimizedCm.next(rql))) {
      optimizedResults.push(std::make_pair(c, rql));
    }
  }
  for (std::pair<Clause*, int> c : oldResults) {
    bool found = false;
    for (std::pair<Clause*,int> opt : optimizedResults) {
      if (opt.first == c.first && opt.second == c.second) {
        found = true;
        break;
      }
    }
    ASS_REP(found, cl->toReproducerString());
  }

  for (std::pair<Clause*, int> opt : optimizedResults) {
    if (opt.second == -1) {
      ASS_REP(satSubs.checkSubsumption(opt.first, cl), cl->toReproducerString());
    } else {
      ASS_REP(satSubs.checkSubsumptionResolutionWithLiteral(opt.first, cl, opt.second), cl->toReproducerString());
    }
  }

  oldCm.reset();
  optimizedCm.reset();
  
  if (optimizedResults.isNonEmpty()) {
    premise = optimizedResults.top().first;
    resolvedQueryLit = optimizedResults.top().second;
  }
  */

  if (premise == nullptr) {
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
