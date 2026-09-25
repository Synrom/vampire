/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"

#include "Indexing/ClauseCodeTree.hpp"
#include "Indexing/TermCodeTree.hpp"
#include "Indexing/Index.hpp"
#include "SATSubsumption/SATSubsumptionAndResolution.hpp"

#include <algorithm>

using namespace std;
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
    DECL_VAR(x8, 8) \
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


namespace {

struct InvariantTester {
  ClauseCodeTree& tree;
  explicit InvariantTester(ClauseCodeTree& tree) : tree(tree) {}

  bool checkAllClausesAppear(std::vector<Clause*> expected) {
    std::vector<Clause*> actual;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isSuccess()) { actual.push_back(op->getSuccessResult<Clause>()); }
    });
    std::sort(actual.begin(), actual.end(), std::less<Clause*>{});
    std::sort(expected.begin(), expected.end(), std::less<Clause*>{});
    ASS(actual == expected);
    return true;
  }

  bool checkSuccessAndFailOperationsAreFinal() {
    Stack<CodeTree::CodeOp*> pending;
    if (!tree.isEmpty()) { pending.push(tree.getEntryPoint()); }
    while (pending.isNonEmpty()) {
      auto first = pending.pop();
      if (first->isSearchStruct()) {
        if (first->alternative()) { pending.push(first->alternative()); }
        for (auto target : first->getSearchStruct()->targets) {
          if (target) { pending.push(target); }
        }
        continue;
      }
      auto block = CodeTree::firstOpToCodeBlock(first);
      for (unsigned i = 0; i < block->length(); ++i) {
        auto op = first + i;
        if (op->alternative()) { pending.push(op->alternative()); }
        if (op->isSuccess() || op->isFail()) { ASS_EQ(i + 1, block->length()); }
        if (op->isLitEnd()) {
          for (const auto& cont : op->getILS()->continuations) { pending.push(cont.entry); }
        }
      }
    }
    return true;
  }

  bool checkNoFailOps() {
    tree.visitAllOps([](const CodeTree::CodeOp* op, unsigned, bool) { ASS(!op->isFail()); });
    return true;
  }

  bool checkNoConsecutiveNextOps() {
    tree.visitAllOps([](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isNext()) { ASS(!op[1].isNext()); }
    });
    return true;
  }

  bool checkILSDepths() {
    tree.visitAllOps([](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isLitEnd()) {
        auto ils = op->getILS();
        ASS_EQ(ils->depth, ils->previous ? ils->previous->depth + 1 : 0);
        ASS_EQ(ils->hasContinuations, ils->continuations.isNonEmpty());
        for (const auto& cont : ils->continuations) {
          ASS(cont.entry);
          if (ils->previous) { ASS_L(cont.slot, ils->previous->successorSlotCount); }
        }
      }
    });
    return true;
  }

  unsigned countNextOps() {
    unsigned count = 0;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) { count += op->isNext(); });
    return count;
  }
};

using Results = std::vector<std::pair<Clause*, int>>;
Results matches(ClauseCodeTree& tree, Clause* query, bool sres) {
  Results result;
  if (tree.isEmpty()) { return result; }
  ClauseCodeTree::ClauseMatcher matcher;
  matcher.init(&tree, query, sres);
  int resolved;
  while (auto cl = matcher.next(resolved)) { result.emplace_back(cl, resolved); }
  matcher.reset();
  std::sort(result.begin(), result.end());
  return result;
}

// Compare shared-tree execution with isolated clauses. This catches changes
// caused by insertion order, shared prefixes, alternatives and removal.
//
// `query` and `result` may both be null. If `query` is null, the check is run once
// per clause in `clauses`, using that clause as the query.
// `result`, when given, must appear among the matches of (tree, query, sres) once
// all clauses have been inserted.
void checkOptimization(std::vector<Clause*> clauses, Clause* query, bool sres, Clause* result = nullptr) {
  if (!query) {
    for (auto cl : clauses) { checkOptimization(clauses, cl, sres); }
    return;
  }
  ClauseCodeTree tree;
  InvariantTester tester(tree);
  std::vector<Clause*> inserted;
  for (auto cl : clauses) {
    tree.insert(cl);
    inserted.push_back(cl);
    ASS(tester.checkNoFailOps());
    ASS(tester.checkSuccessAndFailOperationsAreFinal());
    ASS(tester.checkAllClausesAppear(inserted));
    ASS(tester.checkILSDepths());
    ASS(tester.checkNoConsecutiveNextOps());

    // The clause just inserted must subsume itself.
    auto selfResults = matches(tree, cl, /*sres=*/true);
    ASS(std::find(selfResults.begin(), selfResults.end(), std::make_pair(cl, -1)) != selfResults.end());
  }

  {
    // Validate every match of (tree, query, sres) against the ground truth
    // provided by SAT-based subsumption/resolution checking.
    SATSubsumption::SATSubsumptionAndResolution satSubs;
    auto actual = matches(tree, query, sres);
    bool resultFound = (result == nullptr);
    for (auto& match : actual) {
      Clause* premise = match.first;
      int resolvedQueryLit = match.second;
      if (resolvedQueryLit == -1) {
        ASS(satSubs.checkSubsumption(premise, query));
      } else {
        ASS(satSubs.checkSubsumptionResolutionWithLiteral(premise, query, (unsigned)resolvedQueryLit));
      }
      if (premise == result) { resultFound = true; }
    }
    ASS(resultFound);
  }

  for (;;) {
    ASS(tester.checkAllClausesAppear(clauses));
    ASS(tester.checkILSDepths());
    ASS(tester.checkNoConsecutiveNextOps());
    Results expected;
    for (auto cl : clauses) {
      ClauseCodeTree single;
      single.insert(cl);
      auto singleResult = matches(single, query, sres);
      expected.insert(expected.end(), singleResult.begin(), singleResult.end());
    }
    std::sort(expected.begin(), expected.end());
    ASS(matches(tree, query, sres) == expected);
    if (clauses.empty()) { break; }
    tree.remove(clauses.back());
    clauses.pop_back();
  }
}

// Convenience overload for the common case: no explicit query (each clause is
// checked against every other clause in turn) and no subsumption resolution.
void checkOptimization(std::vector<Clause*> clauses) {
  checkOptimization(std::move(clauses), nullptr, false);
}
}

TEST_FUN(SharedPrefixOverlapVariants)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)

  // C1 = { p(c,d), p(x,e) }, C2 = { p(c,e), p(e,x) }: no shared prefix.
  checkOptimization({ clause({ p2(c,d), p2(x1, e) }), clause({ p2(c,e), p2(e, x1) }) });

  // C1 = { p(c,d), p(c,e) }, C2 = { p(e,d), p(e,e) }: no shared prefix.
  checkOptimization({ clause({p2(c,d), p2(c,e)}), clause({p2(e,d), p2(e,e)}) });

  // C1 = {p(c,c)}, C2 = {p(c,d), p(c,e)}: unit clause plus overlapping binary clause.
  checkOptimization({ clause({p2(c,c)}), clause({p2(c,d), p2(c,e)}) });

  // C1, C2 share their first two literals, C2 has an extra third literal.
  checkOptimization({ clause({p3(c,c,c), p3(c,c,d)}), clause({p3(c,c,c), p3(c,c,d), p3(c,d,d)}) });

  // C1 = {p(c,e,e)}, C2 = {p(c,e,d), p(c,c,e)}: no literal overlap.
  checkOptimization({ clause({p3(c,e,e)}), clause({p3(c,e,d), p3(c,c,e)}) });

  // C1 = { p(c,c), p(c,e) }, C2 = { p(c,d) }: shared prefix then diverges.
  checkOptimization({ clause({p2(c,c), p2(c,e)}), clause({p2(c,d)}) });

  // C1 = { p(c,d), p(e,e) }, C2 = { p(c,d) }: C2 is a strict prefix of C1.
  checkOptimization({ clause({ p2(c,d), p2(e,e) }), clause({ p2(c,d) }) });

  // C1 = { p(c,d,e), p(c,e,e) }, C2 = { p(c,d,e), p(c,d,e2) }: longer shared prefix.
  checkOptimization({ clause({ p3(c,d,e), p3(c,e,e) }), clause({ p3(c,d,e), p3(c,d,e2) }) });

  // p(c,d,e), p(c,d,e2), p(c,e,e): overlap decreases from two arguments to one.
  checkOptimization({ clause({ p3(c,d,e), p3(c,d,e2), p3(c,e,e) }) });

  // p(c,d,e), p(c,e,e), p(d,d,d): overlap, then no overlap at all.
  checkOptimization({ clause({ p3(c,d,e), p3(c,e,e), p3(d,d,d) }) });

  // p(c,d,e), p(c,e,e), p(c,e,e2): overlap increases from one argument to two.
  checkOptimization({ clause({ p3(c,d,e), p3(c,e,e), p3(c,e,e2) }) });

  // Add a clause, then add a strict prefix of it.
  checkOptimization({ clause({p2(c,c), p2(d,d), p2(d,e) }), clause({p2(c,c), p2(d,d) }) });

  // First clause is two non-overlapping literals; second shares those two and
  // adds a third literal overlapping with the first two.
  checkOptimization({ clause({p2(c,c), p2(d,d) }), clause({p2(c,c), p2(d,d), p2(d,c) }) });

  checkOptimization({
    clause({ p(f2(x3,x2)) }),
    clause({ p(f2(x1,f2(x3,x2))), ~p(x2), ~p(x1) }),
    clause({ p(f2(x3,f2(x2,x4))), ~p(x4) })
  });
}

TEST_FUN(NextOpPlacementVariants)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)

  {
    // C1 = { p(c,d), p(c,e) }, C2 = { p(c,d), p(c,e2) }: both clauses must use
    // the shared NEXT op and its LIT_END continuation.
    ClauseCodeTree tree;
    auto c1 = clause({ p2(c,d), p2(c,e) });
    auto c2 = clause({ p2(c,d), p2(c,e2) });
    tree.insert(c1);
    tree.insert(c2);
    InvariantTester tester(tree);
    unsigned binCount = 0;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      if (op->isLitEnd()) { binCount += op->getILS()->continuations.length(); }
    });
    ASS_EQ(tester.countNextOps(), 1);
    ASS_EQ(binCount, 1);
    checkOptimization({ c1, c2 });
  }

  // First add a clause with a next op, then a second clause with a deeper next op at the same depth.
  checkOptimization({ clause({p2(c,c), p2(c,d) }), clause({p2(c,c), p2(f(c),d) }) });

  // Insert a next op after an alternative inside a block.
  checkOptimization({ clause({p2(c,c), p2(d,d) }), clause({p2(c,c), p2(e,e), p2(e,c) }) });

  // First add a clause with a next op, then one without.
  checkOptimization({ clause({p2(c,c), p2(c,d) }), clause({p2(c,c), p2(d,d) }) });

  // First add a clause without a next op, then one with.
  checkOptimization({ clause({p2(c,c), p2(d,d) }), clause({p2(c,c), p2(c,d) }) });
}

TEST_FUN(SearchStruct)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  checkOptimization({
    clause({p2(c,c), p2(c,e) }),
    clause({p2(c,e), p2(e,c) }),
    clause({p2(c,d), p2(c,e) }),
    clause({p2(c,f(c)), p2(c,e) }),
    clause({p2(c,f2(c,c)), p2(c,e) }),
    clause({p2(c,g(c)), p2(c,e) }),
    clause({p2(c,g2(c,c)), p2(c,e) }),
    clause({p2(c,h(c)), p2(c,e) })
  });
}

TEST_FUN(SubsumptionResolutionStructuralVariants)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  checkOptimization({ clause({ p3(c,c,c), p3(c,c,e), p3(c,e,e) }) }, nullptr, /*sres=*/true);

  checkOptimization({
    clause({ ~p(x1), ~p(x2) }),
    clause({ ~p(x1), p(f2(x2,f2(x3,x1))), ~p(f2(x4,x5)), ~p(f2(x5,x2)), ~p(f2(x4,x3)) }),
    clause({ ~p(x1), p(f2(x2,f2(x3,x1))), ~p(f2(x4,x5)), ~p(f2(x5,x2)) })
  }, nullptr, /*sres=*/true);

  checkOptimization({
    clause({ ~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c))) }),
    clause({ p(f2(f2(f2(x1,f2(x2,x3)),f2(x2,x1)),x4)), ~p(x3), ~p(x4) }),
    clause({ ~p(x3) })
  }, nullptr, /*sres=*/true);
}

TEST_FUN(MatchingAgainstExplicitQuery)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)

  checkOptimization(
    { clause({p2(c, x1), p2(x1, d)}), clause({p2(c, x2), p2(f(x2), x2)}) },
    clause({p2(c, d), p2(f(d), d)}), false);

  checkOptimization(
    { clause({p2(c,c), p2(d,d), p2(d,e)}) },
    clause({p2(d,d), p2(d,e), p2(c,c)}), false);

  {
    // Binding propagation across several negated literals must line up with the query.
    auto result = clause({ ~p(f2(x1,f2(x2,x3))), ~p(x4), ~p(x1), ~p(x3) });
    auto D = clause({ ~p(x1), p(f2(x1,x2)), ~p(f2(x3,f2(x2,x4))), ~p(x3), ~p(x4) });
    checkOptimization({ result }, D, /*sres=*/true, result);
  }

  {
    // A long clause must still be found by subsumption after a shorter, unrelated clause is present.
    auto C1 = clause({ p(c), p(d), p(e), p(f(x1)), p(g(x1)), p(h(x1)), p(i(e)), p(e2), p(x1) });
    auto C2 = clause({ q(x1) });
    auto D  = clause({ p(c), p(d), p(e), p(f(x1)), p(g(x1)), p(h(x1)), p(i(e)), p(e2), p(x1) });
    checkOptimization({ C1, C2 }, D, /*sres=*/true, C1);
  }

  {
    // Regression: matching used to crash on this combination of clauses.
    auto result = clause({ ~p(x3) });
    auto D = clause({ ~p(f2(x1,x2)), ~p(x3), p(f2(x1,f2(f2(f2(x4,f2(x5,x2)),f2(x5,x4)),x3))) });
    checkOptimization({
      clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), ~p(f2(x3,x2)) }),
      result,
      clause({ ~p(f2(x3,f2(x2,f2(x4,x1)))), ~p(f2(x2,x3)) })
    }, D, /*sres=*/true, result);
  }
}

TEST_FUN(LazyMatchingWithContinuations)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)

  // The ordinary first literal must eventually consider a later binding when
  // compatibility with the second literal rules out the first one.
  auto ordinary = clause({ p(f(x1)), q(x1) });
  checkOptimization({ ordinary }, clause({ p(f(c)), p(f(d)), q(d) }), false, ordinary);

  // A unit clause and a NEXT continuation share the first literal. Finding the
  // unit must not prevent the binary clause from replaying all its checkpoints.
  auto unit = clause({ p2(f(x1),c) });
  auto continued = clause({ p2(f(x1),c), p2(f(x1),d) });
  ClauseCodeTree tree;
  tree.insert(unit);
  tree.insert(continued);
  ASS_G(InvariantTester(tree).countNextOps(), 0);
  for (bool sres : {false, true}) {
    checkOptimization({ unit, continued },
        clause({ p2(f(c),c), p2(f(d),c), p2(f(d),d) }), sres, continued);
  }
  checkOptimization({ unit, continued },
      clause({ p2(f(c),c), p2(f(d),c), ~p2(f(d),d) }), true, continued);
}

TEST_FUN(lit_end_alternatives)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  checkOptimization({
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,f2(x3,x4))), p(f2(x1,x4)), ~p(x3) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x5,x6)))), ~p(f2(x3,x5)), ~p(x4), ~p(x6) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(f2(x4,f2(x5,x6)),f2(x5,x4)))), ~p(f2(x3,x6)) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,x5))), ~p(f2(x3,x4)), ~p(x5) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,x5))), ~p(x4), ~p(f2(x5,x3)) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x5,x5)))), ~p(f2(x3,x4)) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x5,f2(x4,x3))))), ~p(x5) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x5,x3)))), ~p(f2(x5,x4)) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x5,x3)))), ~p(x5), ~p(x4) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(f2(x4,x4),x3))) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,x3))), ~p(x4) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(x4,f2(x4,x3)))) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), p(f2(x1,f2(f2(x3,x4),f2(x5,x5)))), ~p(x4) })
  });
}

TEST_FUN(remove)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  Kernel::Clause *c1 = clause({p2(c, x1), p2(x1, d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(f(x2), x2)});
  ClauseCodeTree ctree;
  ctree.insert(c1);
  ctree.insert(c2);

  ctree.remove(c2);

  Kernel::Clause *c3 = clause({p2(c, x1), p2(x1, c)});
  ctree.insert(c3);
  ctree.remove(c3);

  Kernel::Clause *c4 = clause({p2(c, x1), p2(x1, d)});
  ctree.insert(c4);
  ctree.remove(c1);

  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});
  ClauseCodeTree::ClauseMatcher m;
  m.init(&ctree, D, false);
  int resolvedQueryLit;
  ASS_EQ(m.next(resolvedQueryLit), 0);
}

TEST_FUN(RemovalRegressions)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  // Regression: removing an added clause used to corrupt an existing clause sharing a prefix.
  checkOptimization({
    clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), ~p(f2(x3,x2)) }),
    clause({ ~p(f2(x2,x1)) })
  });

  // A single clause at removal-depth one must be removable back to an empty tree.
  checkOptimization({ clause({ ~p(f2(x2,x3)) }) });

  {
    // Removal must still work after the literal order of the clause changed
    // in-place between insertion and removal.
    ClauseCodeTree wtree;
    Kernel::Clause* rem4 = clause({ ~p2(c,x2), ~p2(c,f2(x2,x1)) });
    wtree.insert(rem4);
    std::swap((*rem4)[0], (*rem4)[1]);
    wtree.remove(rem4);
    ASS(wtree.isEmpty());
  }

  {
    // Removing a clause that shares a NEXT op with another must only remove itself.
    ClauseCodeTree wtree;
    Kernel::Clause* C1 = clause({ ~p(f2(x2,x1)) });
    wtree.insert(C1);
    Kernel::Clause* C2 = clause({ ~p(f2(x1,f2(x2,x3))), ~p(f2(x2,x1)) });
    wtree.insert(C2);
    wtree.remove(C2);
    InvariantTester tester(wtree);
    ASS(tester.checkAllClausesAppear({ C1 }));
  }

  {
    // A clause using a NEXT op must be fully removable on its own.
    ClauseCodeTree wtree;
    Kernel::Clause* C2 = clause({ ~p(f2(x1,f2(x2,x3))), ~p(f2(x2,x1)) });
    wtree.insert(C2);
    InvariantTester tester(wtree);
    ASS_G(tester.countNextOps(), 0u);
    wtree.remove(C2);
    ASS(wtree.isEmpty());
  }
}

TEST_FUN(RemovePathWithReferencingLitEnd)
{
  // Regression: removing a clause along a path whose LIT_END is referenced as
  // a continuation from an earlier depth used to corrupt the tree.
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  checkOptimization({
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), ~p(f2(x3,x5)), ~p(x4), ~p(x6) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), ~p(f2(x3,x6)) }),
    clause({ ~p(f2(x1,x2)), ~p(f2(x2,x3)), ~p(f2(x3,x4)), ~p(x5) })
  });
}

TEST_FUN(InsertAndRemoveDuplicateClause)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  {
    ClauseCodeTree wtree;
    std::vector<Kernel::Clause*> clauses;
    clauses.push_back(clause({ ~p(x1), ~p(x3) }));
    clauses.push_back(clause({ ~p(f2(x1,x2)), ~p(x4) }));
    for (Kernel::Clause* C : clauses) { wtree.insert(C); }

    Kernel::Clause* C1 = clause({ ~p(x2) });
    wtree.insert(C1);
    wtree.remove(C1);

    InvariantTester tester(wtree);
    ASS(tester.checkAllClausesAppear(clauses));
  }

  {
    ClauseCodeTree wtree;
    std::vector<Kernel::Clause*> clauses;
    clauses.push_back(clause({ ~p(x2), ~p(f2(x2,x1)) }));
    clauses.push_back(clause({ ~p(f2(x1,x2)), ~p(x1) }));
    for (Kernel::Clause* C : clauses) { wtree.insert(C); }

    Kernel::Clause* C1 = clause({ ~p(x2), ~p(f2(x2,x1)) });
    wtree.insert(C1);
    wtree.remove(C1);

    InvariantTester tester(wtree);
    ASS(tester.checkAllClausesAppear(clauses));
  }
}

namespace {
// Check both that survivors remain reachable and that removed SUCCESSes disappear.
void checkRemovalContents(ClauseCodeTree& tree, std::vector<Kernel::Clause*> expected)
{
  InvariantTester tester(tree);
  ASS(tester.checkILSDepths());
  ASS(tester.checkNoConsecutiveNextOps());
  std::vector<Kernel::Clause*> actual;
  tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
    if (op->isSuccess()) {
      actual.push_back(op->getSuccessResult<Kernel::Clause>());
    }
  });
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  ASS(actual == expected);
}

void removeInOrder(std::vector<Kernel::Clause*> clauses, const std::vector<unsigned>& order)
{
  ClauseCodeTree tree;
  for (auto cl : clauses) { tree.insert(cl); }
  auto remaining = clauses;
  for (unsigned i : order) {
    tree.remove(clauses[i]);
    remaining.erase(std::find(remaining.begin(), remaining.end(), clauses[i]));
    checkRemovalContents(tree, remaining);
  }
}
}

TEST_FUN(RemoveContinuationsWithSharedPrefix)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  ClauseCodeTree tree;
  auto cl = clause({p2(c,x1), p2(c,c)});
  tree.insert(cl);
  bool hasNext = false;
  bool hasLazyOnlyContinuation = false;
  tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
    hasNext |= op->isNext();
    if (op->isLitEnd()) {
      hasLazyOnlyContinuation |= !op->getILS()->hasSuccessor &&
          op->getILS()->continuations.isNonEmpty();
    }
  });
  ASS(hasNext);
  ASS(hasLazyOnlyContinuation);
  tree.remove(cl);
  checkRemovalContents(tree, {});
  // Reuse recycled matchers after exhausting the lazy continuation.
  tree.insert(cl);
  tree.remove(cl);
  checkRemovalContents(tree, {});
}

TEST_FUN(RemoveEmptyClauseAndAlternatives)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  auto empty = clause({});
  auto unit = clause({p(c)});
  removeInOrder({empty, unit}, {0, 1});
  removeInOrder({unit, empty}, {1, 0});
}

TEST_FUN(RemoveOverlappingClausesInDifferentOrders)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  std::vector<Kernel::Clause*> clauses = {
    clause({p(x1), q(x1)}), clause({p(x1), q(c)}),
    clause({p(x1), r(x1)}), clause({p(x1), q(x2), r(x2)}),
    clause({p(x1), q(x1), r(c)}), clause({p(x1), p(c)}),
    clause({p(x1)}), clause({p(c), q(c)})
  };
  // Each possible first removal sees all the shared paths still present.
  for (unsigned first = 0; first < clauses.size(); ++first) {
    std::vector<unsigned> order;
    for (unsigned j = 0; j < clauses.size(); ++j) {
      order.push_back((first+j) % clauses.size());
    }
    removeInOrder(clauses, order);
    std::reverse(order.begin(), order.end());
    removeInOrder(clauses, order);
  }
}

TEST_FUN(RemoveSearchStructureBranches)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  std::vector<Kernel::Clause*> clauses = {
    clause({p(f(x1))}), clause({p(g(x1))}), clause({p(h(x1))}),
    clause({p(i(x1))}), clause({p(f2(x1,x2))}), clause({p(g2(x1,x2))}),
    clause({p(h2(x1,x2))}), clause({p(f3(x1,x2,x3))}),
    clause({p(x1)}), clause({p(f(c))})
  };
  for (unsigned first = 0; first < clauses.size(); ++first) {
    ClauseCodeTree tree;
    for (auto cl : clauses) { tree.insert(cl); }
    bool hasSearchStruct = false;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      hasSearchStruct |= op->isSearchStruct();
    });
    ASS(hasSearchStruct);
    auto remaining = clauses;
    for (unsigned j = 0; j < clauses.size(); ++j) {
      auto cl = clauses[(first+j) % clauses.size()];
      tree.remove(cl);
      remaining.erase(std::find(remaining.begin(), remaining.end(), cl));
      checkRemovalContents(tree, remaining);
    }
  }
}

TEST_FUN(RemoveRootSearchStructure)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  std::vector<Kernel::Clause*> clauses = {
    clause({p(x1)}), clause({q(x1)}), clause({r(x1)}),
    clause({p2(x1,x2)}), clause({q2(x1,x2)}), clause({r2(x1,x2)}),
    clause({p3(x1,x2,x3)}), clause({~p(x1)}), clause({~q(x1)})
  };
  ClauseCodeTree tree;
  for (auto cl : clauses) { tree.insert(cl); }
  ASS(tree.getEntryPoint()->alternative()->isSearchStruct());
  // An empty clause is a SUCCESS alternative behind the search structure.
  // Its removal must skip the search-structure frame when looking for a LIT_END.
  auto empty = clause({});
  tree.insert(empty);
  tree.remove(empty);
  checkRemovalContents(tree, clauses);
  auto remaining = clauses;
  for (auto cl : clauses) {
    tree.remove(cl);
    remaining.erase(remaining.begin());
    checkRemovalContents(tree, remaining);
  }
}

TEST_FUN(RemoveAmbiguousLiteralMatches)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  std::vector<Kernel::Clause*> clauses = {
    clause({p2(x1,x2), p2(x2,x1)}),
    clause({p2(x1,x2), p2(x1,x2)}),
    clause({p2(x1,x2), p2(x3,x4)}),
    clause({p2(x1,x1), p2(x2,x2)}),
    clause({p2(x1,x2), p2(x2,x3), p2(x3,x1)}),
    clause({p2(x1,x2), p2(x2,x3), p2(x1,x3)}),
    clause({p2(x1,x2), p2(x2,x3), p2(x4,x5)}),
    clause({p2(x1,x2), p2(x3,x4), p2(x5,x6)})
  };
  for (unsigned first = 0; first < clauses.size(); ++first) {
    std::vector<unsigned> order;
    for (unsigned j = 0; j < clauses.size(); ++j) {
      order.push_back((first+j) % clauses.size());
    }
    removeInOrder(clauses, order);
    std::reverse(order.begin(), order.end());
    removeInOrder(clauses, order);
  }
}

TEST_FUN(RepeatedClauseInsertionMatching)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  auto unit = clause({p(c)});
  checkOptimization({unit, unit, clause({q(c)})}, unit, false);
  checkOptimization({unit, unit, clause({q(c)})}, clause({~p(c), q(c)}), true);
}

// Term trees use the same incorporation and block cleanup, but never NEXT.
TEST_FUN(TermTreeSharedInfrastructure)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  using Data = TermWithValue<unsigned>;
  std::vector<TypedTermList> terms = {
    f(f(x1)), f(g(x1)), f(h(x1)), f(i(x1)), f(f2(x1,x2)),
    f(g2(x1,x2)), f(h2(x1,x2)), f(f3(x1,x2,x3)), f(x1)
  };
  for (unsigned first = 0; first < terms.size(); ++first) {
    TermCodeTree<Data> tree;
    for (unsigned i = 0; i < terms.size(); ++i) { tree.insert(new Data(terms[i], i)); }
    bool hasSearchStruct = false;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      ASS(!op->isNext());
      hasSearchStruct |= op->isSearchStruct();
    });
    ASS(hasSearchStruct);
    std::vector<bool> present(terms.size(), true);
    for (unsigned count = 0; count < terms.size(); ++count) {
      TermCodeTree<Data>::TermMatcher matcher;
      matcher.init(tree, f(f(c)));
      std::vector<unsigned> actual, expected;
      while (auto result = matcher.next()) { actual.push_back(result->value); }
      matcher.reset();
      if (present[0]) { expected.push_back(0); }
      if (present[8]) { expected.push_back(8); }
      std::sort(actual.begin(), actual.end());
      ASS(actual == expected);
      unsigned i = (first + count) % terms.size();
      tree.remove(Data(terms[i], i));
      present[i] = false;
    }
    unsigned successes = 0;
    tree.visitAllOps([&](const CodeTree::CodeOp* op, unsigned, bool) {
      successes += op->isSuccess();
    });
    ASS_EQ(successes, 0);
  }
}
