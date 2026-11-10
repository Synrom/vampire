#include "CodeTreePruningStats.hpp"
#include "CodeTree.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Indexing::CodeTreePruningStats {
namespace {
using Op = CodeTree::CodeOp;
using Count = uint64_t;
constexpr unsigned INF = std::numeric_limits<unsigned>::max()/2;
struct Summary {
  Count successes = 0, operations = 0, branches = 0, literalEnds = 0;
  unsigned distance = INF;
};
struct Features {
  Count tree, path, constraints, codeDepth, literalDepth, literalOps;
  Count ancestorBranches, maxAncestorFanout, subtreeOps, downstreamBranches;
  Count indexedContinuations, literalContinuations, toLiteralEnd;
  Count predecessorContinuations, predecessorFanout;
  bool operator==(const Features&) const = default;
};
struct Row {
  Count site, epoch;
  Features f;
  std::atomic<Count> visits{0}, enabled{0}, rejected{0}, tested{0};
  std::atomic<Count> queryInfos{0}, oppositeVisits{0}, oppositeRejected{0};
  Row(Count site, Count epoch, Features f) : site(site), epoch(epoch), f(f) {}
};
struct TreeState {
  Count id;
  std::vector<const Op*> addresses;
  std::unordered_map<std::string, Count> paths;
  std::unordered_map<std::string, Row*> sites;
};
struct Registry {
  std::mutex mutex;
  Count nextTree = 0, nextPath = 0, nextSite = 0;
  std::unordered_map<const CodeTree*, TreeState> trees;
  std::unordered_map<const void*, Row*> active;
  std::deque<Row> rows;
};
// Initialize before worker/timer threads; keep the registry alive through atexit.
Registry* storage = new Registry;
const bool registered = [] { std::atexit(writeCsv); return true; }();
Registry& registry() { return *storage; }
std::vector<const Op*> children(const Op* op)
{
  std::vector<const Op*> out;
  if (op->alternative()) { out.push_back(op->alternative()); }
  if (op->isSearchStruct()) {
    for (auto t : op->getSearchStruct()->targets) { if (t) { out.push_back(t); } }
  } else if (!op->isSuccess() && !op->isFail()) { out.push_back(op+1); }
  return out;
}
std::string token(const Op* op)
{
  std::ostringstream s;
  s << op->_instruction() << ':';
  if (op->isLitEnd()) {
    auto ils = op->getILS();
    s << ils->depth << ':' << ils->varCnt;
    for (unsigned i=0; i<ils->varCnt; ++i) { s << ':' << ils->globalVarNumbers[i]; }
  } else if (op->isCheckGroundTerm()) { s << op->getTargetTerm(); }
  else { s << op->_arg(); }
  return s.str();
}
}

void refresh(const CodeTree& tree)
{
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto [it, fresh] = r.trees.try_emplace(&tree);
  auto& ts = it->second;
  if (fresh) { ts.id = ++r.nextTree; }
  for (auto address : ts.addresses) { r.active.erase(address); }
  ts.addresses.clear();
  if (tree.isEmpty()) { return; }

  // Iterative postorder: no recursion limit on long clause trees.
  std::unordered_map<const Op*, Summary> summaries;
  std::vector<std::pair<const Op*, bool>> todo{{tree.getEntryPoint(), false}};
  while (!todo.empty()) {
    auto [op, ready] = todo.back(); todo.pop_back();
    if (!ready) {
      todo.emplace_back(op, true);
      for (auto child : children(op)) { todo.emplace_back(child, false); }
      continue;
    }
    Summary s;
    s.successes = op->isSuccess();
    s.operations = !op->isOrderPrune() && !op->isSearchStruct();
    auto edges = children(op);
    s.branches = edges.size() > 1;
    s.literalEnds = op->isLitEnd();
    if (op->isLitEnd()) { s.distance = 0; }
    for (auto child : edges) {
      auto c = summaries.at(child);
      s.successes += c.successes;
      s.operations += c.operations;
      s.branches += c.branches;
      // The fallthrough past LIT_END belongs to the next indexed literal;
      // its alternatives still belong to the current one.
      if (!op->isLitEnd() || child == op->alternative()) {
        s.literalEnds += c.literalEnds;
        s.distance = std::min(s.distance, c.distance +
          unsigned(!op->isOrderPrune() && !op->isSearchStruct()));
      }
    }
    summaries.emplace(op, s);
  }
  struct Path {
    const Op* op;
    Count prefix = 0, depth = 0, literal = 0, literalOps = 0, branches = 0, fanout = 0;
    Count predecessorContinuations = 0, predecessorFanout = 0;
  };
  std::vector<Path> walk{{tree.getEntryPoint()}};
  while (!walk.empty()) {
    auto p = walk.back(); walk.pop_back();
    auto op = p.op;
    auto edges = children(op);
    Count branches = p.branches + (edges.size() > 1);
    Count fanout = std::max<Count>(p.fanout, edges.size());
    if (op->alternative()) {
      auto alt = p; alt.op = op->alternative(); alt.branches = branches; alt.fanout = fanout;
      walk.push_back(alt);
    }
    if (op->isSearchStruct()) {
      for (auto target : op->getSearchStruct()->targets) {
        if (target) { auto t=p; t.op=target; t.branches=branches; t.fanout=fanout; walk.push_back(t); }
      }
      continue;
    }
    if (op->isOrderPrune()) {
      auto s = summaries.at(op+1);
      Features f{ts.id, p.prefix, op->orderConstraints(), p.depth, p.literal, p.literalOps,
        p.branches, p.fanout, s.operations, s.branches, s.successes, s.literalEnds,
        s.distance == INF ? Count(-1) : s.distance,
        p.predecessorContinuations, p.predecessorFanout};
      std::string key = std::to_string(p.prefix)+":"+std::to_string(f.constraints);
      Row*& last = ts.sites[key];
      if (!last || !(last->f == f)) {
        Count site = last ? last->site : ++r.nextSite;
        Count epoch = last ? last->epoch+1 : 0;
        r.rows.emplace_back(site, epoch, f);
        last = &r.rows.back();
      }
      r.active[op] = last;
      ts.addresses.push_back(op);
    } else {
      auto key = std::to_string(p.prefix)+"/"+token(op);
      auto [path, inserted] = ts.paths.try_emplace(key, 0);
      if (inserted) { path->second = ++r.nextPath; }
      p.prefix = path->second;
      p.predecessorContinuations = summaries.at(op).successes;
      p.predecessorFanout = edges.size();
      ++p.depth; ++p.literalOps;
      if (op->isLitEnd()) { ++p.literal; p.literalOps=0; }
    }
    if (!op->isSuccess() && !op->isFail()) {
      p.op = op+1; p.branches = branches; p.fanout = fanout;
      walk.push_back(p);
    }
  }
}

void invalidate(const CodeTree& tree)
{
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto it = r.trees.find(&tree);
  if (it == r.trees.end()) { return; }
  for (auto address : it->second.addresses) { r.active.erase(address); }
  it->second.addresses.clear();
}

void retire(const CodeTree& tree)
{
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto it = r.trees.find(&tree);
  if (it == r.trees.end()) { return; }
  for (auto address : it->second.addresses) { r.active.erase(address); }
  r.trees.erase(it);
}

void record(const void* op, bool enabled, bool rejected, unsigned tested,
            unsigned queryInfos, bool opposite)
{
  // Serialize with timeout-thread output for a coherent snapshot. This is
  // deliberately a diagnostic branch, not a runtime benchmark build.
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto it = r.active.find(op);
  if (it == r.active.end()) { return; }
  auto& row = *it->second;
  row.visits.fetch_add(1, std::memory_order_relaxed);
  row.enabled.fetch_add(enabled, std::memory_order_relaxed);
  row.rejected.fetch_add(rejected, std::memory_order_relaxed);
  row.tested.fetch_add(tested, std::memory_order_relaxed);
  row.queryInfos.fetch_add(queryInfos, std::memory_order_relaxed);
  row.oppositeVisits.fetch_add(opposite, std::memory_order_relaxed);
  row.oppositeRejected.fetch_add(opposite && rejected, std::memory_order_relaxed);
}

uint64_t siteId(const void* op)
{
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto it = r.active.find(op);
  return it == r.active.end() ? 0 : it->second->site;
}

void writeCsv()
{
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  if (!r.nextTree) { return; }
  const char* path = std::getenv("VAMPIRE_KBO_PRUNING_CSV");
  if (!path || !*path) { path = "kbo-pruning.csv"; }
  std::ofstream out(path);
  if (!out) { std::cerr << "Cannot write pruning statistics to " << path << '\n'; return; }
  out << "site_id,feature_epoch,tree_id,path_id,greater_mask,smaller_mask,constraints_added,greater_constraints,smaller_constraints,code_depth,literal_depth,ops_in_literal_before,ancestor_branch_points,max_ancestor_fanout,subtree_ops,downstream_branch_points,indexed_continuations,literal_continuations,min_ops_to_lit_end,predecessor_indexed_continuations,predecessor_fanout,visits,enabled_visits,rejections,constraints_tested,query_infos_sum,opposite_visits,opposite_rejections,rejection_rate,share_of_all_rejections\n";
  Count total = 0;
  for (auto& row : r.rows) { total += row.rejected.load(std::memory_order_relaxed); }
  for (auto& row : r.rows) {
    auto& f = row.f;
    Count greater = f.constraints & ((Count{1} << Op::ORDER_DEPTHS)-1);
    Count smaller = f.constraints >> Op::ORDER_DEPTHS;
    Count enabled = row.enabled.load(), rejected = row.rejected.load();
    out << row.site << ',' << row.epoch << ',' << f.tree << ',' << f.path << ','
      << greater << ',' << smaller << ',' << std::popcount(f.constraints) << ','
      << std::popcount(greater) << ',' << std::popcount(smaller) << ','
      << f.codeDepth << ',' << f.literalDepth << ',' << f.literalOps << ','
      << f.ancestorBranches << ',' << f.maxAncestorFanout << ',' << f.subtreeOps << ','
      << f.downstreamBranches << ',' << f.indexedContinuations << ',' << f.literalContinuations << ',';
    if (f.toLiteralEnd != Count(-1)) { out << f.toLiteralEnd; }
    out << ',' << f.predecessorContinuations << ',' << f.predecessorFanout << ',' << row.visits.load() << ',' << enabled << ',' << rejected << ','
      << row.tested.load() << ',' << row.queryInfos.load() << ',' << row.oppositeVisits.load()
      << ',' << row.oppositeRejected.load() << ',' << std::setprecision(12);
    if (enabled) { out << double(rejected)/enabled; }
    out << ',';
    if (total) { out << double(rejected)/total; }
    out << '\n';
  }
  out.close();
  if (!out) { std::cerr << "Failed writing pruning statistics to " << path << '\n'; }
}
}
