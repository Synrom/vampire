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

#include "Saturation/SaturationAlgorithm.hpp"

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

#include <atomic>
#include <chrono>

namespace Inferences {

namespace {

std::atomic<unsigned> s_inFlightTree{0};
std::atomic<long long> s_inFlightSinceNs{0};

long long nowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

/** marks that the given code tree (1: current, 2: master) is running a query while alive */
struct QueryInFlight {
  explicit QueryInFlight(unsigned tree) {
    s_inFlightSinceNs = nowNs();
    s_inFlightTree = tree;
  }
  ~QueryInFlight() { s_inFlightTree = 0; }
};

} // namespace

void codeTreeQueryInFlight(unsigned& tree, unsigned long long& elapsedNs)
{
  tree = s_inFlightTree;
  long long since = s_inFlightSinceNs;
  elapsedNs = (tree && since) ? (unsigned long long)(nowNs() - since) : 0;
}

CodeTreeForwardSubsumptionAndResolution::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex>()),
    _ct(_index->getClauseCodeTree()),
    _masterCt(_index->getMasterClauseCodeTree())
{}

bool CodeTreeForwardSubsumptionAndResolution::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  // Deliberately a different name from ForwardSubsumptionAndResolution's "forward
  // subsumption": the two are mutually exclusive implementations selected by -cts, so
  // the profile should say which one ran.
  TIME_TRACE("codetree forward subsumption");

  if (_subsumptionResolution) {
    return performWith</*sres=*/true>(cl, replacement, premises);
  } else {
    return performWith</*sres=*/false>(cl, replacement, premises);
  }
}

namespace {

/** What a code tree query found; the values index Statistics::codeTreeComparison. */
enum Outcome { NOTHING = 0, SUBSUMPTION = 1, RESOLUTION = 2 };

inline Outcome outcomeOf(Clause* premise, int resolvedQueryLit)
{
  if (!premise) {
    return NOTHING;
  }
  return resolvedQueryLit == -1 ? SUBSUMPTION : RESOLUTION;
}

const char* outcomeName(Outcome o)
{
  switch (o) {
    case NOTHING:     return "nothing";
    case SUBSUMPTION: return "subsumption";
    case RESOLUTION:  return "subsumption resolution";
  }
  return "?";
}

} // namespace

/**
 * Runs both the current and the master code tree (each exactly once) on the same query, so
 * that their runtimes (see the TIME_TRACEs in the two ClauseMatcher::next) and results can be
 * compared. Only the result of the current code tree is used for the simplification.
 * The master tree runs first in every query.
 */
template<bool sres>
bool CodeTreeForwardSubsumptionAndResolution::performWith(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  // both trees are kept in sync by the index, so they are empty at the same time
  if (_ct->isEmpty()) {
    ASS(_masterCt->isEmpty());
    return false;
  }

  static typename ClauseCodeTree::template ClauseMatcher<sres> cm;
  static Indexing::Master::ClauseCodeTree::ClauseMatcher masterCm;

  // master first, then current (the second call may profit from caches warmed by the first)
  int masterResolvedQueryLit = -1;
  Clause* masterPremise;
  {
    QueryInFlight inFlight(2);
    masterCm.init(_masterCt, cl, sres);
    masterPremise = masterCm.next(masterResolvedQueryLit);
    masterCm.reset();
  }

  int resolvedQueryLit = -1;
  Clause* premise;
  {
    QueryInFlight inFlight(1);
    cm.init(_ct, cl);
    premise = cm.next(resolvedQueryLit);
    cm.reset();
  }

  auto outcome = outcomeOf(premise, resolvedQueryLit);
  auto masterOutcome = outcomeOf(masterPremise, masterResolvedQueryLit);
  env.statistics->codeTreeComparison[outcome][masterOutcome]++;

  // the current code tree must find (at least) whatever master finds; deliberately not an ASS,
  // so that this is also checked in release builds
  if (outcome == NOTHING && masterOutcome != NOTHING) {
    std::cerr << "CodeTree comparison: current found nothing, but master found a "
              << outcomeName(masterOutcome) << "\n"
              << "  query:  " << cl->toString() << "\n"
              << "  master: " << masterPremise->toString() << std::endl;
    std::abort();
  }

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
