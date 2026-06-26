#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"

#include "SATSubsumption/SATSubsumptionAndResolution.hpp"
#include "Indexing/ClauseCodeTree.hpp"

#include <fstream>

using namespace std;
using namespace SATSubsumption;
using namespace Test;
using namespace Indexing;

#define SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION  \
 __ALLOW_UNUSED(                             \
    DECL_DEFAULT_VARS \
    DECL_VAR(x1, 1) \
    DECL_VAR(x2, 2) \
    DECL_VAR(x3, 3) \
    DECL_VAR(x4, 4) \
    DECL_VAR(x5, 5) \
    DECL_VAR(x6, 6) \
    DECL_VAR(x7, 7) \
    DECL_VAR(y1, 11) \
    DECL_VAR(y2, 12) \
    DECL_VAR(y3, 13) \
    DECL_VAR(y4, 14) \
    DECL_VAR(y5, 15) \
    DECL_VAR(y6, 16) \
    DECL_VAR(y7, 17) \
    DECL_SORT(s) \
    DECL_CONST(c, s) \
    DECL_CONST(d, s) \
    DECL_CONST(e, s) \
    DECL_CONST(e2, s) \
    DECL_FUNC(f, {s}, s) \
    DECL_FUNC(f2, {s, s}, s) \
    DECL_FUNC(f3, {s, s, s}, s) \
    DECL_FUNC(g, {s}, s) \
    DECL_FUNC(g2, {s, s}, s) \
    DECL_FUNC(h, {s}, s) \
    DECL_FUNC(h2, {s, s}, s) \
    DECL_FUNC(i, {s}, s) \
    DECL_FUNC(i2, {s, s}, s) \
    DECL_PRED(p, {s}) \
    DECL_PRED(p2, {s, s}) \
    DECL_PRED(p3, {s, s, s}) \
    DECL_PRED(q, {s}) \
    DECL_PRED(q2, {s, s}) \
    DECL_PRED(r, {s}) \
    DECL_PRED(r2, {s, s}) )

TEST_FUN(example1)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause *c1 = clause({p2(c, x1), p2(x1, d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(f(x2), x2)});

  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});

  Kernel::Clause *matched_clause;

  {
    ClauseCodeTree<false> ctree;
    ctree.insert(c1);
    ctree.insert(c2);

    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&ctree, D, false);
    int resolvedQueryLit;
    matched_clause = m.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, -1);
    ASS_EQ(matched_clause, c2);
  }
}

TEST_FUN(insertion)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({p2(c,c), p2(d,d), p2(d,e)});
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << tree << std::endl;
}

// TODO: tests to add
/*
 * - add invariant testers
 */

TEST_FUN(CheckCorrectBins2)
{
  // C1 = {p2(c,c)}, C2 = {p2(c,d), p2(c,e)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({p2(c,c)});
  Kernel::Clause* C2 = clause({p2(c,d), p2(c,e)});
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(CheckCorrectBins1)
{
  // C1 = {p2(c,c), p2(c,e)}, C2 = {p2(c,d)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({p2(c,c), p2(c,e)});
  Kernel::Clause* C2 = clause({p2(c,d)});
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(InsertPrefix2)
{
  // C1 = { p(c,d), p(e,e) }, C2 = { p(c,d) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p2(c,d), p2(e,e) });
  Kernel::Clause* C2 = clause({ p2(c,d) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(InsertLongerPrefixOverlap)
{
  // C1 = { p(c,d,e), p(c,e,e) }, C2 = { p(c,d,e), p(c,d,e2) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p3(c,d,e), p3(c,e,e) });
  Kernel::Clause* C2 = clause({ p3(c,d,e), p3(c,d,e2) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(TwoSameNextOps)
{
  // C1 = { p(c,d), p(c,e) }, C2 = { p(c,d), p(c,e2) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p2(c,d), p2(c,e) });
  Kernel::Clause* C2 = clause({ p2(c,d), p2(c,e2) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(DecreasingOverlapLengths)
{
  // p(c,d,e), p(c,d,e), p(c,e,e)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p3(c,d,e), p3(c,d,e), p3(c,e,e) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << tree << std::endl;
}

TEST_FUN(OverlapAndThenNonOverlap)
{
  // p(c,d,e), p(c,e,e), p(d,d,d)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p3(c,d,e), p3(c,e,e), p3(d,d,d) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << tree << std::endl;
}

TEST_FUN(IncreasingOverlapLengths)
{
  // p(c,d,e), p(c,e,e), p(c,e,b)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({ p3(c,d,e), p3(c,e,e), p3(c,e,e2) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << tree << std::endl;
}

TEST_FUN(AddPrefixOfPriorClause)
{
  // add clause that has three literals with next op between 2nd and third literal. Then add same clause with only 2 literals
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause* C1 = clause({p2(c,c), p2(d,d), p2(d,e) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(d,d) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(NextOpAfterAlternative)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // insert next op after alternative inside a block
  Kernel::Clause* C1 = clause({p2(c,c), p2(d,d) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(e,e), p2(e,c) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(LaterLenghtOverlaps)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first clause is two literals long that are non overlapping. Second clause is three literals long with the first two being the same and the third overlapping
  Kernel::Clause* C1 = clause({p2(c,c), p2(d,d) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(d,d), p2(d,c) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(SeveralNextOpsSameDepth)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first add clause with next op and then a second clause with a deeper next op at same depth
  Kernel::Clause* C1 = clause({p2(c,c), p2(c,d) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(c,c) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(AppendToBlockWithoutSuccessor)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first adding a clause with next op and then one without
  Kernel::Clause* C1 = clause({p2(c,c), p2(c,d) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(d,d) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}

TEST_FUN(AppendToBlockWithSuccessor)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first adding a clause without next op and then one with
  Kernel::Clause* C1 = clause({p2(c,c), p2(d,d) });
  Kernel::Clause* C2 = clause({p2(c,c), p2(c,d) });
  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  std::cout << "Before inserting C2" << std::endl << tree << std::endl;
  tree.insert(C2);
  std::cout << tree << std::endl;
}