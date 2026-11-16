/* See https://vprover.github.io/license.html for the Vampire licence. */
#ifndef VAMPIRE_CODE_TREE_TIME_SAMPLER_HPP
#define VAMPIRE_CODE_TREE_TIME_SAMPLER_HPP

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace Indexing {
/** K independent probability-proportional-to-time draws, with replacement.
 * A new weight w replaces each slot independently with probability w/(W+w).
 * Drawing a binomial replacement count and a uniform subset avoids K random
 * draws on every query. Each final slot selects i with probability w_i / W.
 */
class CodeTreeTimeSampler {
public:
  CodeTreeTimeSampler(unsigned capacity, uint64_t seed) : _capacity(capacity), _rng(seed) {
    if (!capacity) throw std::invalid_argument("Sample capacity must be positive");
  }
  std::vector<unsigned> offer(uint64_t weight) {
    if (!weight || weight > std::numeric_limits<uint64_t>::max() - _total)
      throw std::invalid_argument("Invalid/overflowing sampling weight");
    _total += weight;
    auto count = weight == _total ? _capacity :
      std::binomial_distribution<unsigned>(_capacity, double(weight) / _total)(_rng);
    // Floyd's uniform subset algorithm. Iteration order is deterministic.
    std::set<unsigned> chosen;
    for (unsigned j = _capacity - count; j < _capacity; ++j) {
      auto candidate = std::uniform_int_distribution<unsigned>(0, j)(_rng);
      if (!chosen.insert(candidate).second) chosen.insert(j);
    }
    return {chosen.begin(), chosen.end()};
  }
  uint64_t total() const { return _total; }
private:
  unsigned _capacity;
  uint64_t _total = 0;
  std::mt19937_64 _rng;
};
}
#endif
