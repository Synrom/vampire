#ifndef INDEXING_OPT_EXPERIMENT_HPP
#define INDEXING_OPT_EXPERIMENT_HPP
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <ostream>
#include <string>
#include <algorithm>
#ifndef CT_ORDER_PROFILE
#define CT_ORDER_PROFILE 0
#endif
#if CT_ORDER_PROFILE
#define CT_ORDER_STAT(...) do { __VA_ARGS__; } while (false)
#else
#define CT_ORDER_STAT(...) do {} while (false)
#endif
namespace Indexing::OrderExperiment {
struct Config {
  std::string variant, run;
  bool ordering=false, both=false, eager=false, capacity=false, used=false, cache=false, header=false, terminal=false;
  bool weightSort=false;
  Config();
};
inline const Config& config() { static Config c; return c; }
inline uint64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline uint64_t queries=0, masterOps=0, optimizedOps=0, masterHits=0, optimizedHits=0;
// Rejection events, not saved CodeOps or globally distinct query literals.
// Each failed allowLiteral rejects the current query-LitInfo/branch continuation;
// accepted checks do not increment these counters. A prefix may already be matched.
// Occupancy counts LitInfo attempts skipped before entering the literal matcher.
inline uint64_t greaterPrunes=0, lessPrunes=0, supportPrunes=0, occupancySkipped=0;
inline uint64_t belowPrunes=0, abovePrunes=0, bothCapacityPrunes=0, unionPrunes=0;
// Strict entry means op == Matcher::entry: the root or LIT_END+1.
// Alternatives and search-structure targets are not additional entry points.
// This is an observational subset of the usual rejections, not a separate run
// whose domain completion and traversal might differ under entry-only pruning.
struct EntryPruneCounts {
  uint64_t greater=0, less=0, support=0, below=0, above=0, both=0, unionOnly=0;
  uint64_t total() const { return greater+less+support+below+above+both+unionOnly; }
};
inline EntryPruneCounts entryPrunes;
inline uint64_t rootEntryPrunes=0;
inline uint64_t previousPrunes=0, capacityPrunes=0; // totals for the two constraint families
inline uint64_t differentResults=0, mismatches=0;
inline uint64_t masterNs=0, optimizedNs=0, masterIndexNs=0, optimizedIndexNs=0;
inline uint64_t terminalShortcuts=0;
inline uint64_t queryComparisons=0, insertionComparisons=0, domainBuilds=0, supportBuilds=0;
struct DepthStats {
  uint64_t boundaries=0, informative=0, candidates=0, allowed=0;
  uint64_t greaterRelations=0, lessRelations=0, greaterRemoved=0, lessRemoved=0;
  uint64_t usedRemoved=0, capacityRemoved=0, capacityBoundaries=0, headerRemoved=0, headerBoundaries=0;
  uint64_t lazyDomains=0, forcedEager=0, forcedEagerOps=0, domains[5]={};
};
inline DepthStats depths[65];
inline uint64_t baselineDispatches[65]={}, optimizedDispatches[65]={};
inline uint64_t baselineAttempts[65]={}, optimizedAttempts[65]={};
inline unsigned baseOpBytes=0, optOpBytes=0, baseILBytes=0, optILBytes=0;
inline void print(std::ostream& out) {
  out << "% Order pruning: variant=" << config().variant << " run=" << config().run
      << " sort=" << (config().weightSort ? "weight" : "sharing")
      << " queries=" << queries << " master_ops=" << masterOps
      << " optimized_ops=" << optimizedOps << " master_hits=" << masterHits
      << " optimized_hits=" << optimizedHits << " different_results=" << differentResults
      << " mismatches=" << mismatches << " master_ns=" << masterNs
      << " optimized_ns=" << optimizedNs << " master_index_ns=" << masterIndexNs
      << " optimized_index_ns=" << optimizedIndexNs << " query_comparisons=" << queryComparisons
      << " insertion_comparisons=" << insertionComparisons
      << " terminal_shortcuts=" << terminalShortcuts << " domain_builds=" << domainBuilds << " support_builds=" << supportBuilds << '\n';
  out << "% Order causes: greater=" << greaterPrunes << " less=" << lessPrunes
      << " support=" << supportPrunes << " occupancy_skipped=" << occupancySkipped
      << " depth_below_only=" << belowPrunes << " depth_above_only=" << abovePrunes
      << " depth_both=" << bothCapacityPrunes << " depth_union_only=" << unionPrunes << '\n';
  const uint64_t totalPrunes=greaterPrunes+lessPrunes+supportPrunes+occupancySkipped+
      belowPrunes+abovePrunes+bothCapacityPrunes+unionPrunes;
  auto percent=[](uint64_t part, uint64_t total) { return total ? 100.0*part/total : 0.0; };
  out << "% Order shares: total=" << totalPrunes
      << " greater_pct=" << percent(greaterPrunes,totalPrunes)
      << " less_pct=" << percent(lessPrunes,totalPrunes)
      << " support_pct=" << percent(supportPrunes,totalPrunes)
      << " occupancy_pct=" << percent(occupancySkipped,totalPrunes)
      << " depth_below_pct=" << percent(belowPrunes,totalPrunes)
      << " depth_above_pct=" << percent(abovePrunes,totalPrunes)
      << " depth_both_pct=" << percent(bothCapacityPrunes,totalPrunes)
      << " depth_union_pct=" << percent(unionPrunes,totalPrunes) << '\n';
  out << "% Order entry causes: greater=" << entryPrunes.greater
      << " less=" << entryPrunes.less << " support=" << entryPrunes.support
      << " depth_below_only=" << entryPrunes.below << " depth_above_only=" << entryPrunes.above
      << " depth_both=" << entryPrunes.both << " depth_union_only=" << entryPrunes.unionOnly
      << " total=" << entryPrunes.total() << " root=" << rootEntryPrunes
      << " checks_retained_pct=" << percent(entryPrunes.total(),totalPrunes-occupancySkipped)
      << " occupancy_retained=" << occupancySkipped
      << " total_with_occupancy=" << entryPrunes.total()+occupancySkipped
      << " retained_pct=" << percent(entryPrunes.total()+occupancySkipped,totalPrunes) << '\n';
  out << "% Order layout: base_op=" << baseOpBytes << " opt_op=" << optOpBytes
      << " base_il=" << baseILBytes << " opt_il=" << optILBytes << '\n';
  for (unsigned i=0; i<65; ++i) {
    if (baselineDispatches[i] || optimizedDispatches[i])
      out << "% Order execution: depth=" << i << " baseline_ops=" << baselineDispatches[i]
          << " optimized_ops=" << optimizedDispatches[i] << " baseline_attempts=" << baselineAttempts[i]
          << " optimized_attempts=" << optimizedAttempts[i] << '\n';
  }
  for (unsigned i=0; i<65; ++i) {
    const auto& d=depths[i]; if (!d.boundaries) continue;
    out << "% Order depth: depth=" << i << " boundaries=" << d.boundaries
        << " informative=" << d.informative << " candidates=" << d.candidates
        << " allowed=" << d.allowed << " greater_relations=" << d.greaterRelations
        << " less_relations=" << d.lessRelations << " greater_removed=" << d.greaterRemoved
        << " less_removed=" << d.lessRemoved << " used_removed=" << d.usedRemoved
        << " header_removed=" << d.headerRemoved << " header_boundaries=" << d.headerBoundaries
        << " capacity_removed=" << d.capacityRemoved << " capacity_boundaries=" << d.capacityBoundaries
        << " lazy_domains=" << d.lazyDomains << " forced_eager=" << d.forcedEager
        << " forced_eager_ops=" << d.forcedEagerOps
        << " domains_empty=" << d.domains[0] << " domains_singleton=" << d.domains[1]
        << " domains_two=" << d.domains[2] << " domains_three=" << d.domains[3]
        << " domains_four_plus=" << d.domains[4] << '\n';
  }
}
}
#endif
