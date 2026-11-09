/* Optional experiment instrumentation; no overhead in normal builds. */
#ifndef CODE_TREE_PROFILE_HPP
#define CODE_TREE_PROFILE_HPP
#ifdef CODETREE_ORDER_PROFILE
#include <chrono>
#include <cstdio>
namespace Indexing {
struct CodeTreeProfile {
  inline static double next = 0, setup = 0, compile = 0;
  inline static unsigned long calls = 0;
  static void report() {
    std::fprintf(stderr, "codetree-profile next=%.9f setup=%.9f compile=%.9f calls=%lu\n", next, setup, compile, calls);
  }
};

struct CodeTreeTimer {
  double& target;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit CodeTreeTimer(double& target) : target(target) {}
  ~CodeTreeTimer() { target += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
};
}
#define CT_PROFILE(section) CodeTreeTimer ctTimer(CodeTreeProfile::section)
#define CT_PROFILE_CALL() ++CodeTreeProfile::calls
#else
#define CT_PROFILE(section)
#define CT_PROFILE_CALL()
#endif
#endif
