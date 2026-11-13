/* See https://vprover.github.io/license.html for the Vampire licence. */
#ifndef VAMPIRE_CODE_TREE_DATASET_HPP
#define VAMPIRE_CODE_TREE_DATASET_HPP

#include "Forwards.hpp"
#include <memory>

namespace Indexing {
/** Input-only, opt-in recording. No matcher results or execution counters. */
class CodeTreeDataset {
public:
  static std::unique_ptr<CodeTreeDataset> create();
  ~CodeTreeDataset();
  void insert(Kernel::Clause*, Kernel::Literal* const* ordered);
  void remove(Kernel::Clause*);
  void query(Kernel::Clause*, bool subsumptionResolution);
private:
  struct Impl;
  explicit CodeTreeDataset(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> _impl;
};
}
#endif
