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

#include "Debug/TimeProfiling.hpp"

#include "Lib/Environment.hpp"
#include "Indexing/opt/Experiment.hpp"
#include <cstdlib>

#include "Saturation/SaturationAlgorithm.hpp"

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

namespace Inferences {

CodeTreeForwardSubsumptionAndResolution::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex>()),
    _ct(_index->getClauseCodeTree())
{}

bool CodeTreeForwardSubsumptionAndResolution::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  // Deliberately a different name from ForwardSubsumptionAndResolution's "forward
  // subsumption": the two are mutually exclusive implementations selected by -cts, so
  // the profile should say which one ran.
  TIME_TRACE("codetree forward subsumption");

  using namespace Indexing::OrderExperiment;
  const auto& cfg=config();
  auto optTree=_index->getOptimizedClauseCodeTree();
  if (cfg.run=="optimized" ? optTree->isEmpty() : _ct->isEmpty()) return false;

  static ClauseCodeTree::ClauseMatcher cm;
  static Indexing::BitsetClauseCodeTree::ClauseMatcher optimized;
  int resolvedQueryLit=-1, optimizedResolved=-1;
  Clause* premise=nullptr;
  Clause* optimizedPremise=nullptr;
  uint64_t baseOps=0, optOps=0, baseTime=0, optTime=0;
  auto baseline=[&] {
    auto start=now();
    cm.init(_ct,cl,_subsumptionResolution);
    premise=cm.next(resolvedQueryLit);
    baseOps=cm.executedCodeOps;
    cm.reset();
    baseTime=now()-start;
  };
  auto candidate=[&] {
    auto start=now();
    optimized.init(optTree,cl,_subsumptionResolution);
    optimizedPremise=optimized.next(optimizedResolved);
    optOps=optimized.executedCodeOps;
    optimized.reset();
    optTime=now()-start;
  };
  // Alternate measurement order to reduce systematic warm-cache bias.
  if (cfg.run=="baseline") baseline();
  else if (cfg.run=="optimized") candidate();
  else if (queries%2) { candidate(); baseline(); }
  else { baseline(); candidate(); }
  ++queries;
  masterOps+=baseOps; optimizedOps+=optOps;
  masterNs+=baseTime; optimizedNs+=optTime;
  masterHits+=premise!=nullptr; optimizedHits+=optimizedPremise!=nullptr;
  if (cfg.run=="paired") {
    differentResults+=(premise!=optimizedPremise || (premise && resolvedQueryLit!=optimizedResolved));
    if (premise && !optimizedPremise) {
      ++mismatches;
      print(std::cerr);
      std::cerr << "Order pruning missed query: " << cl->toString() << "\n"
                << "Baseline premise: " << premise->toString() << std::endl;
      std::abort();
    }
    ASS(!optimizedPremise || (optimizedResolved==-1 ?
        satSubs.checkSubsumption(optimizedPremise,cl) :
        satSubs.checkSubsumptionResolutionWithLiteral(optimizedPremise,cl,optimizedResolved)));
  }
  if (cfg.run=="optimized") { premise=optimizedPremise; resolvedQueryLit=optimizedResolved; }

  if (premise) {
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

  return false;
}

} // namespace Inferences
