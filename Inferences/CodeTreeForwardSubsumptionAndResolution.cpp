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

#include "Lib/Random.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

extern bool DEBUG_TRACE_OCT;

namespace Inferences {

template<bool higherOrder>
CodeTreeForwardSubsumptionAndResolution<higherOrder>::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex<higherOrder>>()),
    _optimizedCt(_index->getOptimizedClauseCodeTree())
{}

template<bool higherOrder>
bool CodeTreeForwardSubsumptionAndResolution<higherOrder>::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  if (_optimizedCt->isEmpty()) {
    return false;
  }

  // under randomized simplifications, each subsumption resolution match is with this
  // probability dropped, giving the next match (possibly a proper subsumption, which
  // is never leaky) a chance instead (to be tuned)
  //constexpr double RSI_SKIP_PROB = 0.02;
  //bool rsi = env.options->randomizedSimplifications();

  static typename OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher optimizedCm;

  optimizedCm.init(_optimizedCt, cl, _subsumptionResolution);

  Clause* premise;
  int resolvedQueryLit;
  //std::cout << "Run on " << cl->toReproducerString() << std::endl;

  while ((premise = optimizedCm.next(resolvedQueryLit))) {
    // DIAGNOSTIC: cross-check against the (unoptimized) base ClauseMatcher,
    // which shares the exact same indexed clause set (_ct is maintained in
    // lockstep with _optimizedCt). If the optimized matcher returns a
    // (premise, resolvedQueryLit) pair the base matcher never returns, the
    // bug is in OptimizedClauseMatcher specifically.
    /*
    {
      typename ClauseCodeTree<higherOrder>::ClauseMatcher baseCm;
      baseCm.init(_ct, cl, _subsumptionResolution);
      int baseResolvedLit;
      Clause* basePremise;
      bool baseFound = false;
      while ((basePremise = baseCm.next(baseResolvedLit))) {
        if (basePremise == premise && baseResolvedLit == resolvedQueryLit) {
          baseFound = true;
          break;
        }
      }
      baseCm.reset();
      if (!baseFound) {
        std::cout << "MISMATCH (initCounter=" << initCounter << "): optimized matcher returned (" << premise->toReproducerString()
                   << ", " << resolvedQueryLit << ") for cl=" << cl->toReproducerString()
                   << " but the base matcher never returns this pair" << std::endl;
        ASSERTION_VIOLATION;
      }
    }
    */
    if (resolvedQueryLit == -1) {
      ASS(premise != nullptr);
      ASS_REP(satSubs.checkSubsumption(premise, cl), premise->toReproducerString() + " ||| " + cl->toReproducerString());
      premises = pvi(getSingletonIterator(premise));
      env.statistics->forwardSubsumed++;
      optimizedCm.reset();
      return true;
    }
    //if (rsi && Random::getDouble(0.0,1.0) < RSI_SKIP_PROB) {
    //  continue; // drop this candidate; the next match gets a chance
    //}
    ASS_REP(satSubs.checkSubsumptionResolutionWithLiteral(premise, cl, resolvedQueryLit), cl->toReproducerString());

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
    optimizedCm.reset();
    return true;
  }

  optimizedCm.reset();
  return false;
}

template class CodeTreeForwardSubsumptionAndResolution<false>;
template class CodeTreeForwardSubsumptionAndResolution<true>;

} // namespace Inferences
