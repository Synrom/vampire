/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Test/SyntaxSugar.hpp"
#include "Inferences/CodeTreeForwardSubsumptionAndResolution.hpp"
#include "Inferences/BackwardSubsumptionAndResolution.hpp"

#include "Test/FwdBwdSimplificationTester.hpp"

using namespace std;
using namespace Kernel;
using namespace Inferences;
using namespace Test;

#define MY_SYNTAX_SUGAR  \
 __ALLOW_UNUSED( \
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
    DECL_PRED(r2, {s, s}) \
  )

namespace {

inline auto tester() {
  return FwdBwdSimplification::TestCase<
      CodeTreeForwardSubsumptionAndResolution,
      BackwardSubsumptionAndResolution</*higherOrder=*/false>
    >()
    .options({
      { "backward_subsumption", "on" },
      { "backward_subsumption_resolution", "on" }
    });
}

// Note: most of these tests were taken from the SAT subsumption test suite

// positive subsumptions

TEST_SIMPLIFICATION(pos_sub_test01,
  tester()
    .simplifyWith({ clause({ p2(x, f(y)) }) })
    .toSimplify({ clause({ p2(c, f(d)) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test02,
  tester()
    .simplifyWith({ clause({ ~p2(g(x), f(y)) }) })
    .toSimplify({ clause({ ~p2(g(x), f(x)) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test03,
  tester()
    .simplifyWith({ clause({ p3(x1, x2, x3), p3(f(x2), x4, x4) }) })
    .toSimplify({ clause({ p3(f(c), d, y1), p3(f(d), c, c) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test04,
  tester()
    .simplifyWith({ clause({ p3(x1, x2, x3), p3(f(x2), x4, x4) }) })
    .toSimplify({ clause({ p3(f(c), d, y1), p3(f(d), c, c), r(x1) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test05,
  tester()
    .simplifyWith({ clause({ p(f2(f(g(x1)), x1)), c == g(x1) }) })
    .toSimplify({ clause({ g(y1) == c, p(f2(f(g(y1)), y1)) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test06,
  tester()
    .simplifyWith({ clause({ f2(x1, x2) == c, ~p2(x1, x3), p2(f(f2(x1, x2)), f(x3)) }) })
    .toSimplify({ clause({ c == f2(x3, y2), ~p2(x3, y1), p2(f(f2(x3, y2)), f(y1)) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test07,
  tester()
    .simplifyWith({ clause({ p(f2(f(e), g2(x4, x3))), p2(f2(f(e), g2(x4, x3)), x3), f(e) == g2(x4, x3) }) })
    .toSimplify({ clause({ p(f2(f(e), g2(y1, y3))), p2(f2(f(e), g2(y1, y3)), y3), f(e) == g2(y1, y3) }) })
    .expected({ /* nothing */ })
)

TEST_SIMPLIFICATION(pos_sub_test08,
  tester()
    .simplifyWith({ clause({ p3(y7, f(y1), x4), ~p3(y7, y1, x4) }) })
    .toSimplify({ clause({ p3(x6, f(y3), d), ~p3(x6, y3, d) }) })
    .expected({ /* nothing */ })
)

// negative subsumptions

TEST_SIMPLIFICATION(neg_sub_test01,
  tester()
    .simplifyWith({ clause({ p2(f2(g2(x1, x2), x3), x3), p2(f2(g2(x1, x2), x3), x2), g2(x1, x2) == x3 }) })
    .toSimplify({ clause({ p2(f2(g2(y1, y2), y2), y2), g2(y1, y2) == y2, ~p2(f2(g2(y1, y2), y2), g2(y1, y2)) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test02,
  tester()
    .simplifyWith({ clause({ ~p2(x1, x2), p(x1) }) })
    .toSimplify({ clause({ p(y1), ~p(f(f2(f2(y2, y2), f2(y2, y3)))), ~p(y3), ~p(y2) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test03,
  tester()
    .simplifyWith({ clause({ p2(y5, f(f2(c, x1))), ~p(c), ~p(y5) }) })
    .toSimplify({ clause({ ~q(f(d)), p2(c, f(f2(c, x4))), r(e), ~p(c), d == g(c) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test04,
  tester()
    .simplifyWith({ clause({ p2(y5, f(f2(x1, c))), ~p(c), ~p(y5) }) })
    .toSimplify({ clause({ ~q(d), p2(c, f(f2(x4, c))), r(d), ~p(c), d == g(c) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test05,
  tester()
    .simplifyWith({ clause({ p(x1), x1 == f(x2), p(x2) }) })
    .toSimplify({ clause({ p(y1), y1 == f(y1) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test06,
  tester()
    .simplifyWith({ clause({ p(x1), x1 == f(x2), p(x2), q(x1) }) })
    .toSimplify({ clause({ p(y1), y1 == f(y1), q(y2), r(y3) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_test07,
  tester()
    .simplifyWith({ clause({ p(f(x1)), p(f(x2)) }) })
    .toSimplify({ clause({ p(f(y1)), p(g(y2)) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

// positive subsumption resolutions

TEST_SIMPLIFICATION(pos_sub_res_test01,
  tester()
    .simplifyWith({ clause({ ~p(x1), q(x1) }) })
    .toSimplify({ clause({ p(c), q(c), r(e) }) })
    .expected({ clause({ q(c), r(e) }) })
)

TEST_SIMPLIFICATION(pos_sub_res_test02,
  tester()
    .simplifyWith({ clause({ p2(x1, x2), p2(f(x2), x3) }) })
    .toSimplify({ clause({ ~p2(f(y1), d), p2(g(y1), c), ~p2(f(c), e) }) })
    .expected({ clause({ ~p2(f(y1), d), p2(g(y1), c) }) })
)

TEST_SIMPLIFICATION(pos_sub_res_test03,
  tester()
    .simplifyWith({ clause({ p3(x2, f(x2), e) }) })
    .toSimplify({ clause({ p3(f(e), x5, x5), ~p3(x4, f(x4), e) }) })
    .expected({ clause({ p3(f(e), x5, x5) }) })
)

TEST_SIMPLIFICATION(pos_sub_res_test04,
  tester()
    .simplifyWith({ clause({ p(c) }) })
    .toSimplify({ clause({ ~p(c) }) })
    .expected({ clause({}) })
)

TEST_SIMPLIFICATION(pos_sub_res_test05,
  tester()
    .simplifyWith({ clause({ ~p(f(x1)), q(x1) }) })
    .toSimplify({ clause({ ~p2(x2, x5), q(x2), p(f(x2)), ~q(g(x5)) }) })
    .expected({ clause({ ~p2(x2, x5), q(x2), ~q(g(x5)) }) })
)

// negative subsumption resolutions

TEST_SIMPLIFICATION(neg_sub_res_test01,
  tester()
    .simplifyWith({ clause({ ~p(x1), q(x1) }) })
    .toSimplify({ clause({ p(c), q(d), r(e) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test02,
  tester()
    .simplifyWith({ clause({ ~p(x1), ~q(x2) }) })
    .toSimplify({ clause({ p(c), q(d), r(e) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test03,
  tester()
    .simplifyWith({ clause({ ~p(x1), r(c) }) })
    .toSimplify({ clause({ p(c), q(d), r(e) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test04,
  tester()
    .simplifyWith({ clause({ ~p(x1), p2(x1, x2) }) })
    .toSimplify({ clause({ p(c), q(d), r(e) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test05,
  tester()
    .simplifyWith({ clause({ p3(x1, x2, x2), ~p3(x2, c, c) }) })
    .toSimplify({ clause({ p3(y1, c, c), ~p3(y1, y2, y2) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test06,
  tester()
    .simplifyWith({ clause({ p3(y7, x6, x6), ~p3(y7, d, d) }) })
    .toSimplify({ clause({ p3(x5, d, d), ~p3(x5, x6, x6) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test07,
  tester()
    .simplifyWith({ clause({ ~p3(y7, d, d), p3(y7, x6, x6) }) })
    .toSimplify({ clause({ ~p3(x5, y4, f(f2(x4, f(y3)))), p3(x2, d, d), ~p3(x7, x4, y3), ~p3(x2, f2(x5, y4), x7) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test08,
  tester()
    .simplifyWith({ clause({ ~p3(y7, d, d), p3(y7, x6, x6) }) })
    .toSimplify({ clause({ ~p3(x5, x6, x6), p3(d, d, x5) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test09,
  tester()
    .simplifyWith({ clause({ ~p3(d, y7, d), p3(x6, y7, x6) }) })
    .toSimplify({ clause({ p3(d, x5, d), ~p3(y4, f(y4), f(x5)) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test10,
  tester()
    .simplifyWith({ clause({ ~p3(d, d, d), p3(f(f(y7)), d, y7) }) })
    .toSimplify({ clause({ p3(d, x6, x6) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test11,
  tester()
    .simplifyWith({ clause({ p2(y7, d), p2(e, y7), e == y7 }) })
    .toSimplify({ clause({ ~p2(x6, x5), ~p2(y7, x6), p2(y7, x5) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

TEST_SIMPLIFICATION(neg_sub_res_test12,
  tester()
    .simplifyWith({ clause({ f2(y1, y3) == x1, ~p2(g2(x1, f2(y1, y3)), x1), ~p2(g2(x1, f2(y1, y3)), y1), ~p2(g2(x1, f2(y1, y3)), y3) }) })
    .toSimplify({ clause({ p2(g2(x2, f2(y1, y3)), x2), f2(y1, y3) == x2, p2(g2(x2, f2(y1, y3)), y3) }) })
    .expected({ /* nothing */ })
    .justifications({ /* nothing */ })
)

}

// Regression query observed while testing the ordering/capacity interaction in otter.
TEST_SIMPLIFICATION(order_pruning_multi_literal_domains,
  tester()
    .simplifyWith({ clause({ ~p(x1), ~p(i2(x2,x3)),
      ~p(i2(i2(x4,i2(x5,x3)),i2(x5,x4))), p(i2(x2,x1)) }) })
    .toSimplify({ clause({ ~p(i2(i2(x1,i2(x2,x3)),i2(x2,x1))),
      p(i2(x4,x5)), ~p(x5), ~p(x6), ~p(i2(x4,x3)) }) })
    .expected({})
)

// Differential coverage for boundary masks, opposite polarity, equality orientation,
// prefix sharing, and removal. Each variant is selected in a separate test process.
TEST_FUN(bitset_boundary_differential)
{
  MY_SYNTAX_SUGAR
  Problem problem;
  resetAndFillEnvOptions(OptionMap{}, problem);
  MockedSaturationAlgorithm alg(problem, *env.options);
  Indexing::ClauseCodeTree baseline;
  Indexing::BitsetClauseCodeTree optimized;
  Indexing::ClauseCodeTree::ClauseMatcher cm;
  Indexing::BitsetClauseCodeTree::ClauseMatcher bm;
  std::vector<Lit> pool={p(x), p(f(x)), p(f(f(x))), ~p(x), ~p(g(y)),
      q(x), q(g(y)), ~q(f(x)), f(x)==g(y), g(y)==f(x), f(x)!=g(y),
      p(c), p(f(c)), ~p(d), q(d), f(c)==g(d)};
  uint32_t state=1729;
  auto random=[&]() { state=1664525u*state+1013904223u; return state; };
  auto makeClause=[&](unsigned length) {
    Stack<Lit> lits;
    for (unsigned i=0; i<length; ++i) lits.push(pool[random()%pool.size()]);
    return clause(std::move(lits));
  };
  auto compare=[&](Clause* query) {
    for (bool resolution : {false,true}) {
      cm.init(&baseline,query,resolution); bm.init(&optimized,query,resolution);
      int a=-1,b=-1;
      auto expected=cm.next(a); auto actual=bm.next(b);
      ASS(!expected || actual);
      cm.reset(); bm.reset();
    }
  };
  std::vector<Clause*> stored;
  for (unsigned i=0; i<40; ++i) {
    auto cl=makeClause(1+random()%4);
    stored.push_back(cl); baseline.insert(cl); optimized.insert(cl);
    compare(cl);
    for (unsigned j=0; j<30; ++j) compare(makeClause(1+random()%6));
  }
  for (unsigned i=0; i<stored.size(); i+=2) {
    baseline.remove(stored[i]); optimized.remove(stored[i]);
    for (unsigned j=0; j<30; ++j) compare(makeClause(1+random()%6));
  }
  Ordering::unsetGlobalOrdering();
}

TEST_FUN(bitset_boundary_sizes)
{
  MY_SYNTAX_SUGAR
  Problem problem;
  resetAndFillEnvOptions(OptionMap{}, problem);
  MockedSaturationAlgorithm alg(problem, *env.options);
  Indexing::ClauseCodeTree baseline;
  Indexing::BitsetClauseCodeTree optimized;
  auto indexed=clause({p(f(f(x))),p(f(x)),p(x)});
  baseline.insert(indexed); optimized.insert(indexed);
  for (unsigned length : {3u,63u,64u,65u}) {
    Stack<Lit> lits;
    TermSugar term=c;
    for (unsigned i=0; i<length; ++i) { lits.push(p(term)); term=f(term); }
    auto query=clause(std::move(lits));
    Indexing::ClauseCodeTree::ClauseMatcher cm;
    Indexing::BitsetClauseCodeTree::ClauseMatcher bm;
    cm.init(&baseline,query,true); bm.init(&optimized,query,true);
    int a=-1,b=-1;
    ASS(cm.next(a)); ASS(bm.next(b));
    cm.reset(); bm.reset();
  }
  Ordering::unsetGlobalOrdering();
}
