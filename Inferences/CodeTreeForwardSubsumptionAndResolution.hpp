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
 * @file CodeTreeForwardSubsumptionAndResolution.hpp
 * Defines class CodeTreeForwardSubsumptionAndResolution.
 */

#ifndef __CodeTreeForwardSubsumptionAndResolution__
#define __CodeTreeForwardSubsumptionAndResolution__

#include "Inferences/InferenceEngine.hpp"
#include "Indexing/CodeTreeInterfaces.hpp"
#if VDEBUG
#include "SATSubsumption/SATSubsumptionAndResolution.hpp"
#endif

namespace Inferences {

/**
 * Which code tree is running a query right now (0: none, 1: current, 2: master), and for how
 * long (in ns). Statistics uses this to report a query that is still running when Vampire
 * is terminated (e.g. by the time limit), so that such a "hanging" last query can be recognized.
 * Safe to call from another thread.
 */
void codeTreeQueryInFlight(unsigned& tree, unsigned long long& elapsedNs);

class CodeTreeForwardSubsumptionAndResolution
  : public ForwardSimplificationEngine
{
public:
  CodeTreeForwardSubsumptionAndResolution(SaturationAlgorithm& salg);

  bool perform(Kernel::Clause *cl,
               Kernel::Clause *&replacement,
               Kernel::ClauseIterator &premises) override;

private:
  template<bool sres>
  bool performWith(Kernel::Clause *cl,
               Kernel::Clause *&replacement,
               Kernel::ClauseIterator &premises);

  const bool _subsumptionResolution;
  std::shared_ptr<Indexing::CodeTreeSubsumptionIndex> _index;
  Indexing::ClauseCodeTree* _ct;
  Indexing::Master::ClauseCodeTree* _masterCt;
#if VDEBUG
  SATSubsumption::SATSubsumptionAndResolution satSubs;
#endif
};

}; // namespace Inferences

#endif /* __CodeTreeForwardSubsumptionAndResolution__ */
