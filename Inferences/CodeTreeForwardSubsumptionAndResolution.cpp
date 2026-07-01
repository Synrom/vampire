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

#include <vector>

#include "ProofExtra.hpp"
#include "CodeTreeForwardSubsumptionAndResolution.hpp"

namespace Inferences {

template<bool higherOrder>
CodeTreeForwardSubsumptionAndResolution<higherOrder>::CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg)
  : _subsumptionResolution(salg.getOptions().forwardSubsumptionResolution()),
    _index(salg.getSimplifyingIndex<CodeTreeSubsumptionIndex<higherOrder>>()),
    _ct(_index->getClauseCodeTree()), _oct(_index->getOptimizedClauseCodeTree())
{}

template<bool higherOrder>
bool CodeTreeForwardSubsumptionAndResolution<higherOrder>::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  if (_ct->isEmpty()) {
    return false;
  }

  static typename ClauseCodeTree<higherOrder>::ClauseMatcher cm;
  static typename OptimizedClauseCodeTree<higherOrder>::OptimizedClauseMatcher ocm;

  cm.init(_ct, cl, _subsumptionResolution);
  ocm.init(_oct, cl, _subsumptionResolution);

  Clause* premise;
  int resolvedQueryLit;

  auto printResult = [](const std::pair<Clause*, int>& result) {
    std::cout << result.first->toReproducerString()
              << " resolvedQueryLit=" << result.second << std::endl;
  };
  auto printResults = [&printResult](const char* label, const std::vector<std::pair<Clause*, int>>& results) {
    std::cout << label << " (" << results.size() << "):" << std::endl;
    for (const auto& result : results) {
      std::cout << "  ";
      printResult(result);
    }
  };

  std::vector<std::pair<Clause*, int>> clauseMatcherResults;
  while ((premise = cm.next(resolvedQueryLit))) {
    clauseMatcherResults.emplace_back(premise, resolvedQueryLit);
  }

  std::vector<bool> consumed(clauseMatcherResults.size(), false);
  std::vector<std::pair<Clause*, int>> optimizedClauseMatcherResults;

  //std::cout << "Run on " << cl->toReproducerString() << std::endl;

  while ((premise = ocm.next(resolvedQueryLit))) {
    optimizedClauseMatcherResults.emplace_back(premise, resolvedQueryLit);
    const auto optimizedResult = optimizedClauseMatcherResults.back();
    bool found = false;
    for (size_t i = 0; i < clauseMatcherResults.size(); i++) {
      if (consumed[i]) {
        continue;
      }
      if (clauseMatcherResults[i].first == premise && clauseMatcherResults[i].second == resolvedQueryLit) {
        consumed[i] = true;
        found = true;
        break;
      }
    }
    if (!found) {
      std::cout << "OptimizedClauseMatcher found result not found by ClauseMatcher while executing on clause "
                << cl->toReproducerString() << std::endl;
      std::cout << "  ";
      printResult(optimizedResult);
    }

    if (resolvedQueryLit == -1) {
      ASS(satSubs.checkSubsumption(premise, cl));
      premises = pvi(getSingletonIterator(premise));
      env.statistics->forwardSubsumed++;
      cm.reset();
      ocm.reset();
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
    cm.reset();
    ocm.reset();
    return true;
  }

  for (size_t i = 0; i < clauseMatcherResults.size(); i++) {
    if (!consumed[i]) {
      std::cout << "Executed on clause " << cl->toReproducerString() << std::endl;
      std::cout << "sres = " << _subsumptionResolution << std::endl;
      printResults("ClauseMatcher results", clauseMatcherResults);
      printResults("OptimizedClauseMatcher results", optimizedClauseMatcherResults);
      std::cout << "Result found by ClauseMatcher but not by OptimizedClauseMatcher:" << std::endl;
      std::cout << "  ";
      printResult(clauseMatcherResults[i]);
      ASS_REP(false, "OptimizedClauseMatcher exhausted but ClauseMatcher still has unmatched results");
    }
  }

  cm.reset();
  ocm.reset();
  return false;
}

template class CodeTreeForwardSubsumptionAndResolution<false>;
template class CodeTreeForwardSubsumptionAndResolution<true>;

} // namespace Inferences
