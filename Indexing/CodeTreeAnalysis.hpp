/* See https://vprover.github.io/license.html for the Vampire licence. */
#ifndef VAMPIRE_CODE_TREE_ANALYSIS_HPP
#define VAMPIRE_CODE_TREE_ANALYSIS_HPP

// Only the dedicated analysis build pays for counters. The profile build has
// named, non-inlined instruction bodies, but no counters or per-op clocks.
#ifdef CT_PROFILE
#define CT_PROFILE_INLINE __attribute__((noinline))
#define CT_PROFILE_NOINLINE __attribute__((noinline))
#else
#define CT_PROFILE_INLINE inline
#define CT_PROFILE_NOINLINE
#endif

#ifdef CT_ANALYSIS
#include "CodeTree.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace Indexing {
struct CodeTreeAnalysis {
  using Op = CodeTree::CodeOp;
  struct Node { uint64_t hits = 0; size_t branches = 1; bool terminal = false; };
  inline static CodeTreeAnalysis* current = nullptr;
  std::unordered_map<Op*, Node> nodes;
  uint64_t dispatches = 0, expanded = 0, successScanChecks = 0;
  int maxDepth = -1; // ILStruct depths are zero based; -1 means no LIT_END.

  static size_t alternatives(Op* op) {
    size_t n = 0;
    for (; op; op = op->alternative()) {
      if (op->isSearchStruct()) {
        for (auto t : op->getSearchStruct()->targets) if (t) n += alternatives(t);
      } else ++n;
    }
    return n;
  }

  explicit CodeTreeAnalysis(const CodeTree& tree) {
    std::vector<Op*> todo;
    std::unordered_set<Op*> seen;
    if (!tree.isEmpty()) todo.push_back(tree.getEntryPoint());
    while (!todo.empty()) {
      auto first = todo.back(); todo.pop_back();
      if (!seen.insert(first).second) continue;
      if (first->isSearchStruct()) {
        if (first->alternative()) todo.push_back(first->alternative());
        for (auto t : first->getSearchStruct()->targets) if (t) todo.push_back(t);
        continue;
      }
      auto block = CodeTree::firstOpToCodeBlock(first);
      for (size_t i = 0; i < block->length(); ++i) {
        auto op = &(*block)[i];
        bool terminal = i + 1 == block->length();
        nodes.emplace(op, Node{0, terminal ? 1 : alternatives(op + 1), terminal});
        if (op->alternative()) todo.push_back(op->alternative());
      }
    }
  }

  void hit(Op* op) {
    ++dispatches;
    if (op->isSearchStruct()) {
      // Explicit accounting convention: a lookup costs N virtual operations,
      // one per non-null target. The selected target's subsequent CHECK is
      // also an actual dispatch and is counted separately.
      for (auto t : op->getSearchStruct()->targets) if (t) {
        ++nodes.at(t).hits; ++expanded;
      }
    } else {
      ++nodes.at(op).hits; ++expanded;
      if (op->isLitEnd()) maxDepth = std::max(maxDepth, int(op->getILS()->depth));
    }
  }

  void write(std::ostream& hist, uint64_t query) const {
    std::map<std::pair<std::string, uint64_t>, uint64_t> counts;
    for (auto [op, node] : nodes) {
      ++counts[{"hotness", node.hits}];
      ++counts[{"branching", node.branches}];
      if (!node.terminal) ++counts[{"branching_nonterminal", node.branches}];
    }
    for (auto [key, count] : counts)
      hist << query << ',' << key.first << ',' << key.second << ',' << count << '\n';
  }
};
}
#endif
#endif
