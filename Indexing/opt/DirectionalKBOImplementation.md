# Sparse directional ordering instructions

Base: `optimization/code-tree-literal-occupancy` (`a04fcdf77`).

`ORDER_PRUNE` uses the previously unused instruction tag 7. A static assertion
keeps `CodeOp` at 16 bytes. The packed variant stores 30 current-greater-than
bits followed by 30 current-smaller-than bits in the instruction payload. The
alternative pointer keeps its original representation, but pruning operations
never own alternatives.

Compilation compares indexed literals before renaming, with their shared
clause-variable identities intact. `ILStruct` holds the resulting relations.
A deferred annotation pass walks the tree, intersects relations over each
shared continuation, and inserts only constraints not already checked along
that literal's path. Checks follow structural operations, including divergence
points, rather than being confined to `LIT_END`. Alternative branches inherit
the constraints from before the operation. No ordinary operation retains
ordering metadata: the pass's scratch map is discarded before matching.

Insertion and literal-order optimization skip pruning instructions. Removal
interprets them as no-ops. The next query rebuilds annotations after mutations;
consecutive updates are batched. Compilation is outside `ClauseMatcher::next`. A cache per block/search
structure lets insertion revisit only touched paths, plus descendants whose
inherited constraints changed. Removal and search-structure compression
currently invalidate these caches and can cost O(tree size).
It keeps existing blocks when their length is unchanged and preserves ILS
identities and search-structure lookup keys.

Each query `LitInfo` contains two masks of strict ordering supports. Bits name
LitInfos, preserving equality orientations and opposite polarities separately.
Comparisons use complemented literals for resolution. Same-position pairs and
pairs of two opposite images never supply support. Queries with more than 64
LitInfos and constraints beyond the first 30 indexed depths fall back
conservatively. The existing occupancy filter remains unchanged.

On entry to a literal, each complete earlier domain supplies the union of its
members' query masks. An unfinished lazy domain initially supplies all bits,
so it cannot justify rejection. The instruction intersects the relevant
support sets (or equivalently tests candidate membership in each). Domains
may narrow later; cached supersets only weaken pruning. The global ordering
used for insertion must remain in place throughout the tree's lifetime.

`ClauseMatcher::init(..., useOrdering=false)` bypasses runtime ordering checks
for differential tests; it still executes the same tree and occupancy code.
The focused mutation test compares both modes after updates, with equality,
resolution, multiple supports, shared paths, search structures, and queries
above the bitset capacity.

For diagnostic builds, `-DCODETREE_ORDER_PROFILE=ON` reports cumulative time
inside `ClauseMatcher::next`, query initialization, and insert/remove, along
with the number of next calls. Initialization includes deferred compilation.
The option is off by default and adds no timer code to normal builds.
`scripts/benchmark_directional_kbo.py LABEL` runs both requested commands
serially three times and saves raw logs and JSON measurements.
