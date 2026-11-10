/* Diagnostic instrumentation for the directional-ordering statistics branch. */
#ifndef CODE_TREE_PRUNING_STATS_HPP
#define CODE_TREE_PRUNING_STATS_HPP
#include <cstdint>
namespace Indexing {
class CodeTree;
namespace CodeTreePruningStats {
void refresh(const CodeTree& tree);
void retire(const CodeTree& tree);
void invalidate(const CodeTree& tree);
void record(const void* op, bool enabled, bool rejected, unsigned tested,
            unsigned queryInfos, bool opposite);
void writeCsv();
uint64_t siteId(const void* op);
}
}
#endif
