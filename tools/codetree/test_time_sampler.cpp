/* See https://vprover.github.io/license.html for the Vampire licence. */
#include "Indexing/CodeTreeTimeSampler.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using Indexing::CodeTreeTimeSampler;
int main() {
  constexpr unsigned k = 32, trials = 20000;
  std::array<uint64_t, 4> weights{1, 1, 1, 3}, counts{};
  double estimate = 0;
  for (unsigned seed = 1; seed <= trials; ++seed) {
    CodeTreeTimeSampler sampler(k, seed), scaled(k, seed);
    std::array<unsigned, k> slots{};
    for (unsigned i = 0; i < weights.size(); ++i) {
      auto replaced = sampler.offer(weights[i]);
      if (replaced != scaled.offer(weights[i] * 1000)) throw std::runtime_error("Scale invariance failed");
      for (auto slot : replaced) slots.at(slot) = i;
    }
    for (auto i : slots) { ++counts[i]; estimate += 6.0 / (k * weights[i]); }
    if (sampler.total() != 6) throw std::runtime_error("Wrong total weight");
  }
  for (unsigned i = 0; i < weights.size(); ++i) {
    double p = double(counts[i]) / (k * trials);
    if (std::abs(p - weights[i] / 6.0) > .004) throw std::runtime_error("PPS frequencies incorrect");
  }
  if (std::abs(estimate / trials - 4) > .025) throw std::runtime_error("Inverse probability estimate incorrect");
  std::cout << "PPS test passed: heavy query share " << double(counts[3]) / (k * trials)
            << "; estimated query population " << estimate / trials << '\n';
}
