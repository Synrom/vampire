/* See https://vprover.github.io/license.html for the Vampire licence. */
#ifndef VAMPIRE_CODE_TREE_DATASET_HPP
#define VAMPIRE_CODE_TREE_DATASET_HPP

#include "Forwards.hpp"
#include <memory>
#include <cstdint>

namespace Indexing {
/** Input recording with boundary CPU timing for sampling, never CodeOp counters. */
class CodeTreeDataset {
public:
  static std::unique_ptr<CodeTreeDataset> create();
  ~CodeTreeDataset();
  void insert(Kernel::Clause*, Kernel::Literal* const* ordered);
  void remove(Kernel::Clause*);
  void query(Kernel::Clause*, bool subsumptionResolution, uint64_t cpuNs);
  static uint64_t cpuTimeNs();
  class QueryClock {
  public:
    explicit QueryClock(uint64_t* elapsed) : _elapsed(elapsed), _start(elapsed ? cpuTimeNs() : 0) {}
    ~QueryClock() { stop(); }
    void stop() {
      if (_elapsed) { *_elapsed += cpuTimeNs() - _start; _elapsed = nullptr; }
    }
  private:
    uint64_t* _elapsed;
    uint64_t _start;
  };
  // Used by the caller, not inside ClauseMatcher/Matcher/CodeOp bodies.
  class QueryTimer {
  public:
    explicit QueryTimer(bool enabled) : _enabled(enabled) {}
    template<class Operation> decltype(auto) measure(Operation&& operation) {
      QueryClock clock(_enabled ? &_elapsed : nullptr);
      return operation();
    }
    uint64_t elapsed() const { return _elapsed; }
  private:
    bool _enabled;
    uint64_t _elapsed = 0;
  };
private:
  struct Impl;
  explicit CodeTreeDataset(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> _impl;
};
}
#endif
