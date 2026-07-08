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
  
void checkOptimization(std::vector<Kernel::Clause*> clauses, Kernel::Clause* query, bool sres)
{
  if (query == nullptr) {
    for (Kernel::Clause* D : clauses) {
      checkOptimization(clauses, D, sres);
    }
    return;
  }

  std::vector<std::pair<Kernel::Clause*, int>> results;
  std::vector<std::pair<Kernel::Clause*, int>> optimized_results;

  {
    ClauseCodeTree<false> ctree;
    for (Kernel::Clause* C : clauses) {
      ctree.insert(C);
    }

    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&ctree, query, sres);
    int resolvedQueryLit;
    Kernel::Clause* matched_clause;
    while ((matched_clause = m.next(resolvedQueryLit))) {
      results.push_back(std::make_pair(matched_clause, resolvedQueryLit));
    }
  }

  OptimizedClauseCodeTree<false> ctree;
  for (Kernel::Clause* C : clauses) {
    ctree.insert(C);
  }
  std::cout << ctree << std::endl;

  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&ctree, query, sres);
  int resolvedQueryLit;
  Kernel::Clause* matched_clause;
  while ((matched_clause = m.next(resolvedQueryLit))) {
    optimized_results.push_back(std::make_pair(matched_clause, resolvedQueryLit));
  }
  //std::cout << "Tree is:" << std::endl << ctree << std::endl;

  {
    // check canEnterLiteral and checkCandidate
    ClauseCodeTree<false> ctree;
    for (Kernel::Clause* C : clauses) {
      ctree.insert(C);
    }
    ClauseCodeTree<false>::ClauseMatcher cm;
    cm.init(&ctree, query, sres);
    int resolvedQueryLit;
    cm.next(resolvedQueryLit);

    OptimizedClauseCodeTree<false> otree;
    for (Kernel::Clause* C : clauses) {
      otree.insert(C);
    }
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher om;
    om.init(&otree, query, sres);
    om.next(resolvedQueryLit);
    ASS(om.countCheckCandidate <= cm.countCheckCandidate);
  }

  // check that all normal results are also returned by optimized (completeness)
  for (std::pair<Kernel::Clause*, int> result : results) {
    bool appears = false;
    for (std::pair<Kernel::Clause*, int> optimized_result : optimized_results) {
      if (result == optimized_result) {
        appears = true;
        break;
      }
    }
    if (!appears) {
      std::cout << "Tree is:" << std::endl << ctree << std::endl;
      std::cout << "Normal execution got the results:" << std::endl << "[";
      for (unsigned i=0; i < results.size(); i++) {
        auto result_ = results[i];
        std::cout << "(" << *result_.first << ", " << result_.second << ")";
        if (i != results.size()-1) std::cout << ", ";
      }
      std::cout << "]" << endl;

      std::cout << "Optimized execution got the results:" << std::endl << "[";
      for (unsigned i=0; i < optimized_results.size(); i++) {
        auto result_ = optimized_results[i];
        std::cout << "(" << *result_.first << ", " << result_.second << ")";
        if (i != results.size()-1) std::cout << ", ";
      }
      std::cout << "]" << endl;
      std::cout << "The result (" << *result.first << ", " << result.second << ") was found by normal execution but not by optimized execution" << std::endl;
    }
    ASS(appears);
  }

  // check that all optimized results are also returned by normal (soundness)
  for (std::pair<Kernel::Clause*, int> optimized_result : optimized_results) {
    bool appears = false;
    for (std::pair<Kernel::Clause*, int> result : results) {
      if (result == optimized_result) {
        appears = true;
        break;
      }
    }
    if (!appears) {
      std::cout << "Tree is:" << std::endl << ctree << std::endl;
      std::cout << "Normal execution got the results:" << std::endl << "[";
      for (unsigned i=0; i < results.size(); i++) {
        auto result_ = results[i];
        std::cout << "(" << *result_.first << ", " << result_.second << ")";
        if (i != results.size()-1) std::cout << ", ";
      }
      std::cout <<  "]" <<endl;

      std::cout << "Optimized execution got the results:" << std::endl << "[";
      for (unsigned i=0; i < optimized_results.size(); i++) {
        auto result_ = optimized_results[i];
        std::cout << "(" << *result_.first << ", " << result_.second << ")";
        if (i != results.size()-1) std::cout << ", ";
      }
      std::cout <<  "]" <<endl;
      std::cout << "The result (" << *optimized_result.first << ", " << optimized_result.second << ") was found by optimized execution but not by normal execution" << std::endl;
    }
    ASS(appears);
  }

  // check that subsumption results come before subsumption resolution results
  if (sres) {
    unsigned sres_idx;
    bool sres_appears = false;
    for (unsigned i=0; i < optimized_results.size(); i++) {
      if (optimized_results[i].second != -1) {
        sres_appears = true;
        sres_idx = i;
      }
    }
    if (sres_appears) {
      bool wrong_order = false;
      for (unsigned i=0; i < sres_idx; i++) {
        if (optimized_results[i].second != -1) {
          wrong_order = true;
        }
      }
      for (unsigned i=sres_idx; i < optimized_results.size(); i++) {
        if (optimized_results[i].second == -1) {
          wrong_order = true;
        }
      }
      if (wrong_order) {
        std::cout << "Tree is:" << std::endl << ctree << std::endl;
        std::cout << "Optimized execution got the results:" << std::endl << "[";
        for (unsigned i=0; i < optimized_results.size(); i++) {
          auto result_ = optimized_results[i];
          std::cout << "(" << *result_.first << ", " << result_.second << ")";
          if (i != results.size()-1) std::cout << ", ";
        }
        std::cout <<  "]" <<endl;
        std::cout << "These results are in the wrong order. All subsumption results must come before subsumption resolution results" << std::endl;
      }
      ASS(!wrong_order);
    }
  }
}

TEST_FUN(example1)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c, x1), p2(x1, d)}));
  clauses.push_back(clause({p2(c, x2), p2(f(x2), x2)}));
  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});
  checkOptimization(clauses, D, false);
}

TEST_FUN(insertion)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(d,d), p2(d,e)}));
  Kernel::Clause* D = clause({p2(d,d), p2(d,e), p2(c,c)});
  checkOptimization(clauses, D, false);
}

TEST_FUN(CheckCorrectBin3)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,d), p2(c,e)}));
  clauses.push_back(clause({p2(e,d), p2(e,e)}));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(CheckCorrectBins2)
{
  // C1 = {p2(c,c)}, C2 = {p2(c,d), p2(c,e)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c)}));
  clauses.push_back(clause({p2(c,d), p2(c,e)}));
  checkOptimization(clauses, nullptr, false);
}


TEST_FUN(EdgeCaseAllMatchersHaveExecutedBeforeGoingToNextOne)
{
  // C1 = {p2(c,c)}, C2 = {p2(c,d), p2(c,e)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* C1 = clause({p2(c,c), p2(c,d)});
  wtree.insert(C1);
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  Kernel::Clause* D = clause({p2(c,d), p2(c,c) });
  m.init(&wtree, D, false);
  int resolvedQueryLit;
  Kernel::Clause* premise = m.next(resolvedQueryLit);
  ASS_EQ(premise, C1);
  ASS_EQ(resolvedQueryLit, -1);
}

TEST_FUN(CheckCorrectBins4)
{
  // C1 = {p2(c,c)}, C2 = {p2(c,d), p2(c,e)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p3(c,e,e)}));
  clauses.push_back(clause({p3(c,e,d), p3(c,c,e)}));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(CheckCorrectBins1)
{
  // C1 = {p2(c,c), p2(c,e)}, C2 = {p2(c,d)}
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(c,e)}));
  clauses.push_back(clause({p2(c,d)}));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(InsertPrefix2)
{
  // C1 = { p(c,d), p(e,e) }, C2 = { p(c,d) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p2(c,d), p2(e,e) }));
  clauses.push_back(clause({ p2(c,d) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(InsertLongerPrefixOverlap)
{
  // C1 = { p(c,d,e), p(c,e,e) }, C2 = { p(c,d,e), p(c,d,e2) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p3(c,d,e), p3(c,e,e) }));
  clauses.push_back(clause({ p3(c,d,e), p3(c,d,e2) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(LongerOverlaps)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p3(c,d,e), p3(c,e,e) }));
  clauses.push_back(clause({ p3(c,d,e), p3(e,e,e) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(TwoSameNextOps)
{
  // C1 = { p(c,d), p(c,e) }, C2 = { p(c,d), p(c,e2) }
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p2(c,d), p2(c,e) }));
  clauses.push_back(clause({ p2(c,d), p2(c,e2) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(DecreasingOverlapLengths)
{
  // p(c,d,e), p(c,d,e), p(c,e,e)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p3(c,d,e), p3(c,d,e), p3(c,e,e) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(OverlapAndThenNonOverlap)
{
  // p(c,d,e), p(c,e,e), p(d,d,d)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p3(c,d,e), p3(c,e,e), p3(d,d,d) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(IncreasingOverlapLengths)
{
  // p(c,d,e), p(c,e,e), p(c,e,b)
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p3(c,d,e), p3(c,e,e), p3(c,e,e2) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(AddPrefixOfPriorClause)
{
  // add clause that has three literals with next op between 2nd and third literal. Then add same clause with only 2 literals
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(d,d), p2(d,e) }));
  clauses.push_back(clause({p2(c,c), p2(d,d) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(NextOpAfterAlternative)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // insert next op after alternative inside a block
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(d,d) }));
  clauses.push_back(clause({p2(c,c), p2(e,e), p2(e,c) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(LaterLenghtOverlaps)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first clause is two literals long that are non overlapping. Second clause is three literals long with the first two being the same and the third overlapping
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(d,d) }));
  clauses.push_back(clause({p2(c,c), p2(d,d), p2(d,c) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(LaterLenghtOverlaps2)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first clause is two literals long that are non overlapping. Second clause is three literals long with the first two being the same and the third overlapping
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(d,d) }));
  clauses.push_back(clause({p2(d,d), p2(d,d) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(SameDepthDifferentPrefixLitEnds)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first clause is two literals long that are non overlapping. Second clause is three literals long with the first two being the same and the third overlapping
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(c,d) }));
  clauses.push_back(clause({p2(c,e), p2(c,e2) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(SeveralNextOpsSameDepth)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first add clause with next op and then a second clause with a deeper next op at same depth
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(c,d) }));
  clauses.push_back(clause({p2(c,c), p2(c,c) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(AppendToBlockWithoutSuccessor)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first adding a clause with next op and then one without
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(c,d) }));
  clauses.push_back(clause({p2(c,c), p2(d,d) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(AppendToBlockWithSuccessor)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
  // first adding a clause without next op and then one with
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(d,d) }));
  clauses.push_back(clause({p2(c,c), p2(c,d) }));
  checkOptimization(clauses, nullptr, false);
}

bool checkSubsumption(Kernel::Clause *l, Kernel::Clause *m, bool verbose)
{
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(l);
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(l);

  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher wm;
  if (verbose) {
    std::cout << wtree << std::endl;
  }
  wm.init(&wtree, m, true);
  int resolvedQueryLit;
  Kernel::Clause *matched_clause;
  matched_clause = wm.next(resolvedQueryLit);
  return matched_clause != 0 && resolvedQueryLit == -1;
}

Kernel::Clause* checkSubsumptionResolution(Kernel::Clause *l, Kernel::Clause *m, bool verbose)
{
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(l);
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(l);

  if (verbose) {
    std::cout << wtree << std::endl;
  }
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher wm;
  wm.init(&wtree, m, true);
  int resolvedQueryLit;
  Kernel::Clause *premise;
  while ((premise = wm.next(resolvedQueryLit))) {
    if (resolvedQueryLit == -1) {
      continue;
    }
    LiteralStack res;
    for (unsigned i = 0; i < m->length(); i++) {
      if (i == (unsigned)resolvedQueryLit) {
        continue;
      }
      res.push((*m)[i]);
    }
    Kernel::Clause *replacement = Clause::fromStack(res, SimplifyingInference2(InferenceRule::FORWARD_SUBSUMPTION_RESOLUTION, m, premise));
    return replacement;
  }
  return 0; 
}

static bool checkClauseEquality(Literal const* const lits1[], unsigned len1, Literal const* const lits2[], unsigned len2)
{
  if (len1 != len2) {
  }
  // Copy given literals so we can sort them
  std::vector<Literal const*> c1(lits1, lits1 + len1);
  std::vector<Literal const*> c2(lits2, lits2 + len2);
  // The equality tests only make sense for shared literals
  std::for_each(c1.begin(), c1.end(), [](Literal const* lit) { ASS(lit->shared()); });
  std::for_each(c2.begin(), c2.end(), [](Literal const* lit) { ASS(lit->shared()); });
  // Sort input by pointer value
  // NOTE: we use std::less<> because the C++ standard guarantees it is a total order on pointer types.
  //       (the built-in operator< is not required to be a total order for pointer types.)
  std::less<Literal const*> const lit_ptr_less{};
  std::sort(c1.begin(), c1.end(), lit_ptr_less);
  std::sort(c2.begin(), c2.end(), lit_ptr_less);
  // Finally check the equality
  return c1 == c2;
}

static bool checkClauseEquality(Clause* const cl1, Clause* const cl2)
{
  return checkClauseEquality(cl1->literals(), cl1->length(), cl2->literals(), cl2->length());
}

TEST_FUN(remove)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;
  Kernel::Clause *c1 = clause({p2(c, x1), p2(x1, d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(f(x2), x2)});

  OptimizedClauseCodeTree<false> ctree;
  ctree.insert(c1);
  ctree.insert(c2);
  std::cout << ctree << std::endl;

  ctree.remove(c2);
  std::cout << "After removing " << *c2 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *c3 = clause({p2(c, x1), p2(x1, c)});
  ctree.insert(c3);
  ctree.remove(c3);
  std::cout << "After inserting and removing " << *c3 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *c4 = clause({p2(c, x1), p2(x1, d)});
  std::cout << "After inserting " << *c4 << std::endl;
  ctree.insert(c4);
  std::cout << ctree << std::endl;
  ctree.remove(c1);
  std::cout << "After removing first " << *c1 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&ctree, D, false);
  int resolvedQueryLit;
  ASS_EQ(m.next(resolvedQueryLit), 0);
}

TEST_FUN(remove2)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;
  Kernel::Clause *c1 = clause({p2(c, x1), p2(f(x2), d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(c, d)});

  OptimizedClauseCodeTree<false> ctree;
  ctree.insert(c1);
  ctree.insert(c2);
  std::cout << ctree << std::endl;

  ctree.remove(c2);
  std::cout << "After removing " << *c2 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *c3 = clause({p2(c, x1), p2(c, c)});
  ctree.insert(c3);
  ctree.remove(c3);
  std::cout << "After inserting and removing " << *c3 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *c4 = clause({p2(c, x1), p2(c, d)});
  std::cout << "After inserting " << *c4 << std::endl;
  ctree.insert(c4);
  std::cout << ctree << std::endl;

  ctree.remove(c1);
  std::cout << "After removing first " << *c1 << std::endl;
  std::cout << ctree << std::endl;

  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&ctree, D, false);
  int resolvedQueryLit;
  ASS_EQ(m.next(resolvedQueryLit), 0);
}

TEST_FUN(example2)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;
  Kernel::Clause *c1 = clause({p2(c, x1), p2(x1, d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(f(x2), x2)});

  Kernel::Clause *D = clause({p2(c, d), p2(f(d), d)});

  Kernel::Clause *matched_clause;

  {
    ClauseCodeTree<false> ctree;
    ctree.insert(c1);
    ctree.insert(c2);

    ofstream recording("output/clause_code_tree_example1_execution.json");
    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&ctree, D, false);
    int resolvedQueryLit;
    matched_clause = m.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, -1);
    ASS_EQ(matched_clause, c2);
  }

  {
    OptimizedClauseCodeTree<false> wtree;
    wtree.insert(c1);
    wtree.insert(c2);
    std::cout << "Resulting wide clause code tree:" << std::endl;
    std::cout << wtree << std::endl;

    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher wm;
    wm.init(&wtree, D, false);
    int resolvedQueryLit;
    matched_clause = wm.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, -1);
    ASS_EQ(matched_clause, c2);
  }
}

TEST_FUN(try_bug)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;
  Kernel::Clause *c1 = clause({p2(c, x2), p2(f(x2), x2)});
  Kernel::Clause *D = clause({ p2(f(d), d), p2(c, d), p2(c,c) });
  Kernel::Clause *matched_clause;

  {
    ClauseCodeTree<false> ctree;
    ctree.insert(c1);
    std::cout << "Tree is" << std::endl;
    std::cout << ctree << std::endl;

    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&ctree, D, false);
    int resolvedQueryLit;
    matched_clause = m.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, -1);
    ASS_EQ(matched_clause, c1);
  }
  {
    OptimizedClauseCodeTree<false> ctree;
    ctree.insert(c1);
    std::cout << "Tree is" << std::endl;
    std::cout << ctree << std::endl;

    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&ctree, D, false);
    int resolvedQueryLit;
    matched_clause = m.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, -1);
    ASS_EQ(matched_clause, c1);
  }
}

TEST_FUN(example_sres)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;
  Kernel::Clause *c1 = clause({p2(c, x1), p2(x1, d)});
  Kernel::Clause *c2 = clause({p2(c, x2), p2(f(x2), x2)});

  Kernel::Clause *D = clause({ p2(c,x3), ~p2(f(x3), x3) });

  Kernel::Clause *matched_clause;

  {
    ClauseCodeTree<false> ctree;
    ctree.insert(c1);
    ctree.insert(c2);

    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&ctree, D, true);
    int resolvedQueryLit;
    matched_clause = m.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, 1);
    ASS_EQ(matched_clause, c2);
  }

  {
    OptimizedClauseCodeTree<false> wtree;
    wtree.insert(c1);
    wtree.insert(c2);
    std::cout << "Resulting wide clause code tree:" << std::endl;
    std::cout << wtree << std::endl;

    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher wm;
    wm.init(&wtree, D, true);
    int resolvedQueryLit;
    matched_clause = wm.next(resolvedQueryLit);
    ASS_EQ(resolvedQueryLit, 1);
    ASS_EQ(matched_clause, c2);
  }
}

TEST_FUN(PositiveSubsumption) // TODO: make database out of single clauses and check that given clause is one of the clauses returned by next
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;

  std::vector<Kernel::Clause*> L;
  std::vector<Kernel::Clause*> M;

  // Test 1
  L.push_back(clause({ p3(x1, x2, x3), p3(f(x2), x4, x4) }));
  M.push_back(clause({ p3(f(c), d, y1), p3(f(d), c, c) }));

  // Test 2
  L.push_back(clause({ p3(x1, x2, x3), p3(f(x2), x4, x4) }));
  M.push_back(clause({ p3(f(c), d, y1), p3(f(d), c, c), r(x1) }));

  // Test 3
  L.push_back(clause({ p(f2(f(g(x1)), x1)), c == g(x1) }));
  M.push_back(clause({ g(y1) == c, p(f2(f(g(y1)), y1)) }));

  // Test 4
  L.push_back(clause({ f2(x1, x2) == c, ~p2(x1, x3), p2(f(f2(x1, x2)), f(x3)) }));
  M.push_back(clause({ c == f2(x3, y2), ~p2(x3, y1), p2(f(f2(x3, y2)), f(y1)) }));

  // Test 5
  L.push_back(clause({ p(f2(f(e), g2(x4, x3))), p2(f2(f(e), g2(x4, x3)), x3), f(e) == g2(x4, x3) }));
  M.push_back(clause({ p(f2(f(e), g2(y1, y3))), p2(f2(f(e), g2(y1, y3)), y3), f(e) == g2(y1, y3) }));

  // Test 6
  L.push_back(clause({ p3(y7, f(y1), x4), ~p3(y7, y1, x4) }));
  M.push_back(clause({ p3(x6, f(y3), d), ~p3(x6, y3, d) }));

  // // Test 7
  // L.push_back(clause({~p(x1), p(f(x1))}));
  // M.push_back(clause({~p(x1), p(f(f(x1)))}));

  bool success = true;
  for (unsigned k = 0; k < L.size(); k++) {
    if (!checkSubsumption(L[k], M[k], k==1)) {
      std::cerr << "Test " << k + 1 << " failed" << std::endl;
      success = false;
    }
  }

  ASS(success)
}

TEST_FUN(NegativeSubsumption)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    SATSubsumptionAndResolution subsumption;

  std::vector<Kernel::Clause*> L;
  std::vector<Kernel::Clause*> M;

  // Test 1
  L.push_back(clause({ p2(f2(g2(x1, x2), x3), x3), p2(f2(g2(x1, x2), x3), x2), g2(x1, x2) == x3 }));
  M.push_back(clause({ p2(f2(g2(y1, y2), y2), y2), g2(y1, y2) == y2, ~p2(f2(g2(y1, y2), y2), g2(y1, y2)) }));

  // Test 2
  L.push_back(clause({ ~p2(x1, x2), p(x1) }));
  M.push_back(clause({ p(y1), ~p(f(f2(f2(y2, y2), f2(y2, y3)))), ~p(y3), ~p(y2) }));

  // Test 3
  L.push_back(clause({ p2(y5, f(f2(c, x1))), ~p(c), ~p(y5) }));
  M.push_back(clause({ ~q(f(d)), p2(c, f(f2(c, x4))), r(e), ~p(c), d == g(c) }));

  // Test 4
  L.push_back(clause({ p2(y5, f(f2(x1, c))), ~p(c), ~p(y5) }));
  M.push_back(clause({ ~q(d), p2(c, f(f2(x4, c))), r(d), ~p(c), d == g(c) }));

  // Test 5
  L.push_back(clause({ p(x1), x1 == f(x2), p(x2) }));
  M.push_back(clause({ p(y1), y1 == f(y1) }));

  // Test 6
  L.push_back(clause({ p(x1), x1 == f(x2), p(x2), q(x1) }));
  M.push_back(clause({ p(y1), y1 == f(y1), q(y2), r(y3) }));

  // Test 7
  L.push_back(clause({ p(f(x1)), p(f(x2)) }));
  M.push_back(clause({ p(f(y1)), p(g(y2)) }));

  bool success = true;
  for (unsigned k = 0; k < L.size(); k++) {
    std::cout << "Test " << k << std::endl;
    if (checkSubsumption(L[k], M[k], k==6)) {
      std::cerr << "Test " << k + 1 << " failed" << std::endl;
      success = false;
    }
  }

  ASS(success)
}

TEST_FUN(PositiveSubsumptionResolution)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    Kernel::Clause* conclusion;
  SATSubsumptionAndResolution subsumption;

  std::vector<Kernel::Clause*> L;
  std::vector<Kernel::Clause*> M;
  std::vector<Kernel::Clause*> expected;

  // Test 1
  L.push_back(clause({ ~p(x1), q(x1) }));
  M.push_back(clause({ p(c), q(c), r(e) }));
  expected.push_back(clause({ q(c), r(e) }));

  // Test 2
  L.push_back(clause({ p2(x1, x2), p2(f(x2), x3) }));
  M.push_back(clause({ ~p2(f(y1), d), p2(g(y1), c), ~p2(f(c), e) }));
  expected.push_back(clause({ ~p2(f(y1), d), p2(g(y1), c) }));

  // Test 3
  L.push_back(clause({ p3(x2, f(x2), e) }));
  M.push_back(clause({ p3(f(e), x5, x5), ~p3(x4, f(x4), e) }));
  expected.push_back(clause({ p3(f(e), x5, x5) }));

  // Test 4
  L.push_back(clause({ p(c) }));
  M.push_back(clause({ ~p(c) }));
  expected.push_back(clause({}));

  // Test 5
  L.push_back(clause({ ~p(f(x1)), q(x1) }));
  M.push_back(clause({ ~p2(x2, x5), q(x2), p(f(x2)), ~q(g(x5)) }));
  expected.push_back(clause({ ~p2(x2, x5), q(x2), ~q(g(x5)) }));

  // Test 6
  L.push_back(clause({ p(f2(y7, x6)), p2(y7, x6) }));
  M.push_back(clause({ p2(f(g(x5)), y4), ~p(f2(f(g(x5)), y4)), ~p2(f2(f(g(x5)), y4), x5) }));
  expected.push_back(nullptr);

  // Test 7
  L.push_back(clause({ p(f2(y7, x6)), p2(y7, x6) }));
  M.push_back(clause({ p2(f(g(x5)), y4), ~p(f2(f(g(x5)), y4)), ~p2(f2(f(g(x5)), y4), x5) }));
  expected.push_back(nullptr);

  // Test 8
  L.push_back(clause({ ~p2(y7, d) }));
  M.push_back(clause({ ~p(x6), ~p(x5), ~p2(f(f2(f2(x5, x5), f2(x5, x6))), x6), p2(f2(f2(x5, x5), f2(x5, x6)), d) }));
  expected.push_back(nullptr);

  // Test 9
  L.push_back(clause({ ~p3(d, y7, d), ~p3(y7, e, c) }));
  M.push_back(clause({ p3(d, x6, d), ~p3(x6, e, c) }));
  expected.push_back(nullptr);

  // Test 10
  L.push_back(clause({ p2(y7, x6), ~q2(x5, x6), ~p2(y7, x5) }));
  M.push_back(clause({ p2(f2(y4, x6), y4), ~p2(x6, y7), ~r2(y4, y7), ~p2(x6, d), ~q2(y4, d), ~q2(y7, d) }));
  expected.push_back(nullptr);

  // Test 11
  L.push_back(clause({ p(d) }));
  M.push_back(clause({ q(d), ~p(d) }));
  expected.push_back(nullptr);

  bool success = true;

  for (unsigned k = 0; k < L.size(); k++) {
    conclusion = checkSubsumptionResolution(L[k], M[k], false);
    if (conclusion == nullptr) {
      std::cerr << "Test " << k + 1 << " failed: no conclusion generated" << std::endl;
      success = false;
    }
    if (expected[k] != nullptr && conclusion != nullptr) {
      if (!checkClauseEquality(conclusion, expected[k])) {
        std::cerr << "Test " << k << " failed: wrong conclusion" << std::endl;
        std::cerr << "Expected: " << expected[k]->toString() << std::endl;
        std::cerr << "Actual: " << conclusion->toString() << std::endl;
        success = false;
      }
    }
  }

  ASS(success)
}

TEST_FUN(NegativeSubsumptionResolution)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION)
    Kernel::Clause* conclusion;
  SATSubsumptionAndResolution subsumption;

  std::vector<Kernel::Clause*> L;
  std::vector<Kernel::Clause*> M;

  // Test 1
  L.push_back(clause({ ~p(x1), q(x1) }));
  M.push_back(clause({ p(c), q(d), r(e) }));

  // Test 2
  L.push_back(clause({ ~p(x1), ~q(x2) }));
  M.push_back(clause({ p(c), q(d), r(e) }));

  // Test 3
  L.push_back(clause({ ~p(x1), r(c) }));
  M.push_back(clause({ p(c), q(d), r(e) }));

  // Test 4
  L.push_back(clause({ ~p(x1), p2(x1, x2) }));
  M.push_back(clause({ p(c), q(d), r(e) }));

  // Test 5
  L.push_back(clause({ p3(x1, x2, x2), ~p3(x2, c, c) }));
  M.push_back(clause({ p3(y1, c, c), ~p3(y1, y2, y2) }));

  // Test 6
  L.push_back(clause({ p3(y7, x6, x6), ~p3(y7, d, d) }));
  M.push_back(clause({ p3(x5, d, d), ~p3(x5, x6, x6) }));

  // Test 7
  L.push_back(clause({ ~p3(y7, d, d), p3(y7, x6, x6) }));
  M.push_back(clause({ ~p3(x5, y4, f(f2(x4, f(y3)))), p3(x2, d, d), ~p3(x7, x4, y3), ~p3(x2, f2(x5, y4), x7) }));

  // Test 8
  L.push_back(clause({ ~p3(y7, d, d), p3(y7, x6, x6) }));
  M.push_back(clause({ ~p3(x5, x6, x6), p3(d, d, x5) }));

  // Test 9
  L.push_back(clause({ ~p3(d, y7, d), p3(x6, y7, x6) }));
  M.push_back(clause({ p3(d, x5, d), ~p3(y4, f(y4), f(x5)) }));

  // Test 10
  L.push_back(clause({ ~p3(d, d, d), p3(f(f(y7)), d, y7) }));
  M.push_back(clause({ p3(d, x6, x6) }));

  // Test 11
  L.push_back(clause({ p2(y7, d), p2(e, y7), e == y7 }));
  M.push_back(clause({ ~p2(x6, x5), ~p2(y7, x6), p2(y7, x5) }));

  // Test 12
  L.push_back(clause({ f2(y1, y3) == x1, ~p2(g2(x1, f2(y1, y3)), x1), ~p2(g2(x1, f2(y1, y3)), y1), ~p2(g2(x1, f2(y1, y3)), y3) }));
  M.push_back(clause({ p2(g2(x2, f2(y1, y3)), x2), f2(y1, y3) == x2, p2(g2(x2, f2(y1, y3)), y3) }));

  bool success = true;

  for (unsigned k = 0; k < L.size(); k++) {
    conclusion = checkSubsumptionResolution(L[k], M[k], k == 11);
    if (conclusion != nullptr) {
      std::cerr << "Test " << k + 1 << " failed: conclusion generated" << std::endl;
      std::cerr << "Conclusion: " << conclusion->toString() << std::endl;
      success = false;
    }
  }

  ASS(success)
}

TEST_FUN(UsePreviousSettings)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  Kernel::Clause* conclusion = nullptr;

  // subsumption works but not SR
  Kernel::Clause* L1 = clause({ p(x1), q(x2) });
  Kernel::Clause* M1 = clause({ p(c), q(y1) });
  ASS(checkSubsumption(L1, M1, false));
  conclusion = checkSubsumptionResolution(L1, M1, false);
  ASS(!conclusion);

  // subsumption and SR don't work
  Kernel::Clause* L2 = clause({ p(x1), r(x2) });
  Kernel::Clause* M2 = clause({ p(c), q(y1) });
  ASS(!checkSubsumption(L2, M2, false));
  conclusion = checkSubsumptionResolution(L2, M2, false);
  ASS(!conclusion);

  // SR works but not subsumption
  Kernel::Clause* L3 = clause({ p(x1), q(x2) });
  Kernel::Clause* M3 = clause({ p(y1), ~q(c) });
  Kernel::Clause* expected3 = clause({ p(y1) });
  ASS(!checkSubsumption(L3, M3, false));
  conclusion = checkSubsumptionResolution(L3, M3, false);
  ASS(conclusion);
  ASS(checkClauseEquality(conclusion, expected3));

  Kernel::Clause* L4 = clause({ p2(y3, y5), ~q2(x7, y5), ~p2(y3, x7) });
  Kernel::Clause* M4 = clause({ p2(f2(y2, y5), y2), ~p2(y5, y3), ~r2(y2, y3), ~p2(y5, c), ~q2(y2, c), ~q2(y3, c) });
  ASS(!checkSubsumption(L4, M4, false));
  conclusion = checkSubsumptionResolution(L4, M4, false);
  ASS(conclusion);

  Kernel::Clause* L5 = clause({ p3(y1, y5, x7), ~p3(x2, x3, x7), ~p3(y3, y5, x3), ~p3(x2, y3, y1) });
  Kernel::Clause* M5 = clause({ p3(x1, x6, y4), ~p3(y2, x4, y4), ~p3(y6, x4, x6), ~p3(x1, y6, y2) });
  ASS(!checkSubsumption(L5, M5, false));
  conclusion = checkSubsumptionResolution(L5, M5, false);
  ASS(!conclusion);
}

TEST_FUN(PaperExample)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  Kernel::Clause* conclusion;
  SATSubsumptionAndResolution subsumption;

  std::vector<Kernel::Clause*> L;
  std::vector<Kernel::Clause*> M;
  std::vector<Kernel::Clause*> expected;

  // Test 1
  L.push_back(clause({ p2(f(x1), x2), ~p2(x2, x1), p2(f(x3), x1) }));
  M.push_back(clause({ ~p2(f(c), d), ~p2(d, c), p2(f(y1), c) }));
  expected.push_back(clause({ ~p2(d, c), p2(f(y1), c) }));

  bool success = true;

  for (unsigned k = 0; k < L.size(); k++) {
    conclusion = checkSubsumptionResolution(L[k], M[k], false);
    if (conclusion == nullptr) {
      std::cerr << "Test " << k + 1 << " failed: no conclusion generated" << std::endl;
      success = false;
    }
    if (expected[k] != nullptr && conclusion != nullptr) {
      if (!checkClauseEquality(conclusion, expected[k])) {
        std::cerr << "Test " << k << " failed: wrong conclusion" << std::endl;
        std::cerr << "Expected: " << expected[k]->toString() << std::endl;
        std::cerr << "Actual: " << conclusion->toString() << std::endl;
        success = false;
      }
    }
  }

  ASS(success)
}

TEST_FUN(ChangeLiteralOrderBetweenInsertionAndRemoval)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  OptimizedClauseCodeTree<false> wtree;

  Kernel::Clause* rem4 = clause({ ~p(x2), ~p(f2(x2,x1)) });
  wtree.insert(rem4);
  std::swap((*rem4)[0], (*rem4)[1]);
  std::cout << "Before removal:" << std::endl;
  std::cout << wtree << std::endl;
  wtree.remove(rem4);
  std::cout << "After removal:" << std::endl;
  std::cout << wtree << std::endl;
}

TEST_FUN(ReproduceSoundness)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  // So eventho there is a subsumption, both will return a subsumption resolution here
  // That's a mistake of checkCandidate not perferring subsumptions over subsumption resolutions
  ClauseCodeTree<false> wtree;
  Kernel::Clause *C = clause({~p(f2(x2,x1)), ~p(x2), p(x1)});
  Kernel::Clause *D =  clause({~p(f2(x1,x2)), ~p(x3), p(x4), ~p(f2(x3,x4)), ~p(x1), ~p(x2)});

  {
    OptimizedClauseCodeTree<false> tree;
    tree.insert(C);
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    std::cout << tree << std::endl;
    m.init(&tree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause != 0 && resolvedQueryLit != -1);
    m.reset();
  }

  {
    ClauseCodeTree<false> tree;
    tree.insert(C);
    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&tree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause == 0 || resolvedQueryLit != -1);
    m.reset();
  }

  C->destroy();
  D->destroy();
}

TEST_FUN(LongRemoval)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  Kernel::Clause *C1 = clause({ p(c), p(d), p(e), p(f(x1)), p(g(x1)), p(h(x1)), p(i(e)), p(e2), p(x1) });
  Kernel::Clause *C2 = clause({ q(x1) });
  Kernel::Clause *D = clause({ p(c), p(d), p(e), p(f(x1)), p(g(x1)), p(h(x1)), p(i(e)), p(e2), p(x1) });

  OptimizedClauseCodeTree<false> tree;
  tree.insert(C1);
  tree.insert(C2);
  std::cout << "After insert:" << std::endl;
  std::cout << tree << std::endl;

  {
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&tree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause != 0 && resolvedQueryLit == -1);
    m.reset();
  }

  tree.remove(C1);
  std::cout << "After removal:" << std::endl;
  std::cout << tree << std::endl;

  {
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&tree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause == 0);
    m.reset();
  }

}

TEST_FUN(SearchStruct)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  /*
  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* C1 = clause({p2(c,c), p2(c,e) });
  Kernel::Clause* C2 = clause({p2(c,e), p2(c,e) });
  wtree.insert(C1);
  std::cout << wtree << std::endl;
  wtree.insert(C2);
  std::cout << wtree << std::endl;
  */

  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({p2(c,c), p2(c,e) }));
  clauses.push_back(clause({p2(c,e), p2(c,e) }));
  clauses.push_back(clause({p2(c,d), p2(c,e) }));
  clauses.push_back(clause({p2(c,f(c)), p2(c,e) }));
  clauses.push_back(clause({p2(c,f2(c,c)), p2(c,e) }));
  clauses.push_back(clause({p2(c,g(c)), p2(c,e) }));
  clauses.push_back(clause({p2(c,g2(c,c)), p2(c,e) }));
  clauses.push_back(clause({p2(c,h(c)), p2(c,e) }));
  checkOptimization(clauses, nullptr, false);
}


TEST_FUN(ReproducerSortAfterCanEnterLiteralFails)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({ p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2)))) }));
  wtree.insert(clause({ ~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c))) }));
  wtree.insert(clause({ p(x1), ~p(x2), ~p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(x1), p(f2(f2(x2,f2(x3,x1)),f2(x3,x2))) }));
  wtree.insert(clause({ ~p(x1), ~p(f2(x2,f2(x3,x1))), p(f2(x3,x2)) }));
  wtree.insert(clause({ ~p(x1), p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), p(f2(f2(x2,f2(x1,x3)),x3)) }));
  std::cout << wtree << std::endl;

  Kernel::Clause* D =  clause({ ~p(f2(x1,x2)), ~p(x3), p(f2(x4,f2(x2,f2(x1,f2(x4,x3))))) });

  {
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&wtree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause == 0);
    m.reset();
  }

  {
    ClauseCodeTree<false> tree;
    tree.insert(clause({ p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2)))) }));
    tree.insert(clause({ ~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c))) }));
    tree.insert(clause({ p(x1), ~p(x2), ~p(f2(x2,x1)) }));
    tree.insert(clause({ ~p(x1), p(f2(f2(x2,f2(x3,x1)),f2(x3,x2))) }));
    tree.insert(clause({ ~p(x1), ~p(f2(x2,f2(x3,x1))), p(f2(x3,x2)) }));
    tree.insert(clause({ ~p(x1), p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x3) }));
    tree.insert(clause({ ~p(f2(x1,x2)), p(f2(f2(x2,f2(x1,x3)),x3)) }));

    ClauseCodeTree<false>::ClauseMatcher m;
    m.init(&tree, D, true);
    int resolvedQueryLit;
    Kernel::Clause *matched_clause;
    matched_clause = m.next(resolvedQueryLit);
    ASS(matched_clause == 0);
    m.reset();
  }
}

TEST_FUN(ReproducerEalryDepthStoppageSorted)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({ p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2)))) }));
  wtree.insert(clause({ ~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c))) }));
  wtree.insert(clause({ p(x1), ~p(x2), ~p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(x1), p(f2(f2(x2,f2(x3,x1)),f2(x3,x2))) }));
  wtree.insert(clause({ ~p(x1), ~p(f2(x2,f2(x3,x1))), p(f2(x3,x2)) }));
  wtree.insert(clause({ ~p(x1), p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), p(f2(f2(x2,f2(x1,x3)),x3)) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), ~p(x3), p(f2(x4,f2(x2,f2(x1,f2(x4,x3))))) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), ~p(f2(x2,f2(x1,x3))), p(x3) }));
  wtree.insert(clause({ ~p(x1), ~p(x2), ~p(x3), p(f2(x1,f2(x3,x2))) }));
  wtree.insert(clause({ ~p(x1), ~p(x2), ~p(f2(x3,x2)), p(f2(x1,x3)) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), p(x4), ~p(f2(x3,x2)) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x1,x3)))), p(x2), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), p(f2(x2,x3)), ~p(x1), ~p(x3) }));
  std::cout << wtree << std::endl;

  Kernel::Clause *D = clause({ ~p(f2(f2(x1,f2(x2,x3)),x3)), p(f2(x2,x1)) });
  {
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&wtree, D, true);
    int resolvedQueryLit;
    m.next(resolvedQueryLit); // crashed before
  }
}

TEST_FUN(ReproducerEalryDepthStoppageSortedMinimized)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({ ~p(f2(x2,x1)) }));
  std::cout << wtree << std::endl;

  Kernel::Clause* C2 = clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), ~p(f2(x3,x2)) });
  wtree.insert(C2);

  std::cout << wtree << std::endl;

  Kernel::Clause* D = clause({ p(f2(x2,x1)) });
  {
    OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
    m.init(&wtree, D, true);
    int resolvedQueryLit;
    m.next(resolvedQueryLit); // crashed before
  }
}

TEST_FUN(ReproduceRemoveDepthOneRealFailure)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({ ~p(f2(x2,x1)), ~p(x2), p(x1) }));
  wtree.insert(clause({ ~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c))) }));
  wtree.insert(clause({ p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2)))) }));
  wtree.insert(clause({ p(f2(f2(x2,f2(x3,x1)),f2(x3,x2))), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x3,x1))), ~p(x1), p(f2(x3,x2)) }));
  wtree.insert(clause({ p(f2(f2(x2,f2(x1,x3)),x3)), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x1), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x1,x3))), ~p(f2(x1,x2)), p(x3) }));
  wtree.insert(clause({ ~p(f2(x3,x2)), ~p(x2), ~p(x1), p(f2(x1,x3)) }));
  wtree.insert(clause({ p(f2(x1,f2(x3,x2))), ~p(x2), ~p(x3), ~p(x1) }));
  wtree.insert(clause({ ~p(d) }));
  wtree.insert(clause({ p(f2(x4,f2(x2,f2(x1,f2(x4,x3))))), ~p(x3), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ p(f2(x2,x1)), ~p(x2), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),x3)), p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(f2(d,c)) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), p(f2(x2,x3)), ~p(x1), ~p(x3) }));

  Kernel::Clause* D1 = clause({ ~p(x3) });
  wtree.insert(D1);
  wtree.remove(D1);

  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x1,x3)))), p(x2), ~p(x3) }));
  wtree.insert(clause({ p(f2(x1,f2(x2,x2))), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), p(x4), ~p(f2(x3,x2)) }));
  wtree.insert(clause({ p(f2(x2,x2)) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),f2(x2,x1))), ~p(x4), p(f2(x4,x3)) }));
  wtree.insert(clause({ ~p(f2(x2,x1)), ~p(x1), p(x2) }));

  Kernel::Clause* result = clause({ ~p(f2(x1,f2(x2,x3))), ~p(x4), p(f2(x4,x2)), ~p(x1), ~p(x3) });
  wtree.insert(result);

  wtree.insert(clause({ p(f2(f2(x2,x2),x3)), ~p(x3) }));
  wtree.insert(clause({ p(f2(x3,f2(x2,f2(x1,x4)))), ~p(x3), ~p(f2(x1,x2)), ~p(x4) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x1,x2))), p(x2) }));
  wtree.insert(clause({ p(f2(x2,f2(x2,x1))), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x3,x4)), ~p(f2(x2,x3)), ~p(x1), p(f2(x2,f2(x4,x1))) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x4,x1))), ~p(f2(x2,x3)), ~p(x1), p(f2(x3,x4)) }));
  wtree.insert(clause({ ~p(f2(f2(x2,x2),x1)), p(x1) }));
  wtree.insert(clause({ p(f2(x3,f2(x2,f2(x4,x1)))), ~p(f2(x2,x3)), ~p(x4), ~p(x1) }));

  Kernel::Clause* D3 = clause({ ~p(f2(x2,x1)), ~p(x2) });
  wtree.insert(D3);
  wtree.remove(D3);

  wtree.insert(clause({ ~p(f2(x3,f2(x2,f2(x4,x1)))), ~p(f2(x2,x3)), ~p(x1), ~p(x5), p(f2(x5,x4)) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(f2(x2,x3)), ~p(x1), ~p(f2(x2,x1)), p(x3) }));
  wtree.insert(clause({ p(f2(f2(x1,x2),x1)), ~p(x2) }));
  wtree.insert(clause({ ~p(f2(x2,x3)), ~p(x1), ~p(x3), p(f2(x2,x1)) }));
  wtree.insert(clause({ p(f2(f2(x1,x2),x3)), ~p(x1), ~p(x2), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x3,x2))), ~p(x2), ~p(x3), p(x1) }));
  wtree.insert(clause({ ~p(f2(x2,f2(f2(x3,x1),f2(x2,x3)))), p(x1) }));
  wtree.insert(clause({ ~p(f2(f2(x2,x1),x2)), p(x1) }));
  wtree.insert(clause({ p(f2(x3,f2(x2,x4))), ~p(f2(x2,x3)), ~p(x4) }));
  wtree.insert(clause({ ~p(c) }));
  wtree.insert(clause({ ~p(f2(x2,x5)), p(f2(x5,x1)), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), ~p(x1), ~p(x4), p(f2(x4,x2)) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x3,x3)))), p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x1,f2(x4,x3)))), ~p(x3), ~p(x4), p(f2(x1,x2)) }));
  wtree.insert(clause({ p(f2(f2(x1,f2(x1,x2)),x2)) }));
  wtree.insert(clause({ p(f2(f2(f2(x1,f2(x2,x3)),f2(x2,x1)),x4)), ~p(x3), ~p(x4) }));
  wtree.insert(clause({ p(f2(x1,f2(x1,f2(x2,x2)))) }));
  wtree.insert(clause({ p(f2(f2(x1,f2(x2,x3)),x4)), ~p(x2), ~p(x4), ~p(x1), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,x2))), p(x1) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x1,x4))), p(f2(f2(x1,x2),x3)), ~p(x3), ~p(x4) }));
  wtree.insert(clause({ p(f2(x1,f2(x2,f2(x2,x1)))) }));
  wtree.insert(clause({ ~p(f2(x3,f2(x4,x1))), p(f2(x1,x2)), ~p(x2), ~p(f2(x4,x3)) }));
  wtree.insert(clause({ p(f2(f2(x1,f2(x2,x2)),x1)) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x4,x1))), ~p(f2(x2,x3)), p(x1), ~p(x3), ~p(x4) }));
  wtree.insert(clause({ p(f2(f2(x1,x1),f2(x2,x2))) }));
  wtree.insert(clause({ ~p(f2(x3,f2(x2,f2(x4,x1)))), ~p(f2(x2,x3)), ~p(x4), p(x1) }));
  wtree.insert(clause({ ~p(f2(x3,f2(x4,x2))), p(f2(x1,x2)), ~p(x3), ~p(x4), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x2,f2(x3,x1)))), p(x1), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x3,f2(x4,x2))), p(f2(x1,x2)), ~p(x1), ~p(f2(x4,x3)) }));
  wtree.insert(clause({ ~p(f2(x3,f2(x3,x2))), p(f2(x1,x2)), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x3,f2(f2(x3,x2),x1))), ~p(x2), p(x1) }));
  wtree.insert(clause({ ~p(f2(x3,x1)), ~p(f2(x2,x3)), ~p(x4), ~p(x1), p(f2(x2,x4)) }));
  wtree.insert(clause({ ~p(f2(f2(x2,x1),x3)), p(x1), ~p(x3), ~p(x2) }));
  wtree.insert(clause({ ~p(f2(x2,x4)), ~p(f2(x2,x3)), ~p(x4), ~p(x1), p(f2(x3,x1)) }));
  wtree.insert(clause({ ~p(f2(x2,f2(x3,x1))), ~p(x2), p(x1), ~p(x3) }));
  wtree.insert(clause({ p(f2(f2(x3,f2(x2,x4)),x5)), ~p(x5), ~p(f2(x2,x3)), ~p(x4) }));
  wtree.insert(clause({ ~p(f2(x3,x1)), ~p(f2(x2,x3)), p(x1), ~p(x2) }));
  wtree.insert(clause({ ~p(f2(x2,x4)), ~p(f2(x2,x3)), ~p(x5), p(f2(x5,x3)), ~p(x4) }));
  wtree.insert(clause({ ~p(f2(f2(x3,x1),f2(x2,x3))), ~p(x2), p(x1) }));
  wtree.insert(clause({ ~p(f2(x1,x2)), ~p(x3), p(f2(x1,f2(x4,x3))), ~p(x2), ~p(x4) }));
  wtree.insert(clause({ p(f2(x2,f2(x1,x2))), ~p(x1) }));
  wtree.insert(clause({ p(f2(x2,f2(x4,f2(x3,x1)))), ~p(f2(x3,x4)), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ p(f2(f2(x1,x2),x2)), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(x2,x3)), ~p(f2(x1,x2)), p(x1), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),x4)), ~p(x1), p(f2(x4,x2)), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(f2(x1,x2),x3)), ~p(f2(x3,x1)), p(x2) }));
  wtree.insert(clause({ ~p(f2(x4,x3)), ~p(x3), p(f2(x2,x4)), ~p(x1), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ ~p(f2(x2,x3)), p(f2(f2(x1,x1),x2)), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(x3,x1)), ~p(x3), ~p(f2(x2,x4)), p(x4), ~p(f2(x1,x2)) }));
  wtree.insert(clause({ p(f2(f2(x1,f2(x2,x3)),x3)), ~p(x2), ~p(x1) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),x3)), ~p(x2), p(x1) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),x3)), ~p(x1), ~p(x4), p(f2(x4,x2)) }));
  wtree.insert(clause({ ~p(f2(x5,x1)), ~p(x2), ~p(x4), ~p(f2(x1,x2)), p(f2(x4,x5)) }));
  wtree.insert(clause({ ~p(f2(f2(x4,x4),x2)), ~p(f2(x2,x3)), p(x3) }));
  wtree.insert(clause({ p(f2(f2(x1,f2(x2,f2(x3,x4))),x5)), ~p(x3), ~p(x5), ~p(x4), ~p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(f2(x4,f2(x3,x5))), p(f2(x1,x2)), ~p(x3), ~p(x1), ~p(x5), ~p(f2(x4,x2)) }));
  wtree.insert(clause({ ~p(f2(f2(x1,f2(x2,x3)),f2(x2,x1))), p(x3) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,x3))), p(x2), ~p(x1), ~p(x3) }));
  wtree.insert(clause({ ~p(f2(f2(x1,x2),f2(x3,x4))), ~p(f2(x1,x3)), ~p(x2), ~p(x5), p(f2(x5,x4)) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x3,f2(x4,x5))))), ~p(f2(x4,x3)), ~p(x5), p(f2(x2,x1)) }));
  wtree.insert(clause({ ~p(f2(x1,f2(x2,f2(x3,x4)))), ~p(x5), p(f2(x1,f2(x4,x5))), ~p(f2(x3,x2)) }));

  Kernel::Clause* D2 = clause({ ~p(f2(x2,x3)) });
  wtree.insert(D2);
  wtree.remove(D2);

  std::cout << wtree << std::endl;

  Kernel::Clause* D = clause({ ~p(f2(x1,x2)), ~p(x3), p(f2(x1,f2(f2(f2(x4,f2(x5,x2)),f2(x5,x4)),x3))) });
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&wtree, D, true);
  int resolvedQueryLit;
  m.next(resolvedQueryLit); // crashed before
}

TEST_FUN(ReproduceInsertionBug)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ p(f2(x3,x2)) }));
  clauses.push_back(clause({ p(f2(x1,f2(x3,x2))), ~p(x2), ~p(x1) }));
  clauses.push_back(clause({ p(f2(x3,f2(x2,x4))), ~p(x4) }));
  checkOptimization(clauses, nullptr, false);
}

TEST_FUN(ReproduceRemoveDepthOne)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* D2 = clause({ ~p(f2(x2,x3)) });
  wtree.insert(D2);
  wtree.remove(D2);
  std::cout << wtree << std::endl;
  ASS(wtree.isEmpty());
}

TEST_FUN(sres_fail)
{
  /*
   * This just showcases that the prior Clause Code Trees can return sres before subsumption eventhough there is a subsumption
   */
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  Kernel::Clause* C1 = clause({ p2(c,e), p(c) });
  Kernel::Clause* C2 = clause({ ~p(c) });
  Kernel::Clause* D = clause({ p2(c,e), ~p(c) });

  ClauseCodeTree<false> tree;
  tree.insert(C1);
  tree.insert(C2);

  ClauseCodeTree<false>::ClauseMatcher m;
  m.init(&tree, D, true);
  int resolvedQueryLit;
  Kernel::Clause* matched_clause = m.next(resolvedQueryLit);
  ASS_EQ(resolvedQueryLit, 1);
  ASS_EQ(matched_clause, C1);
}

TEST_FUN(Reproducer)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2))))}));
  wtree.insert(clause({~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c)))}));
  wtree.insert(clause({p(x1), ~p(x2), ~p(f2(x2,x1))}));
  wtree.insert(clause({~p(x1), p(f2(f2(x2,f2(x3,x1)),f2(x3,x2)))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,f2(x3,x1))), p(f2(x3,x2))}));
  wtree.insert(clause({~p(x1), p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x3)}));
  wtree.insert(clause({~p(f2(x1,x2)), p(f2(f2(x2,f2(x1,x3)),x3))}));
  wtree.insert(clause({~p(f2(x1,x2)), ~p(x3), p(f2(x4,f2(x2,f2(x1,f2(x4,x3)))))}));
  wtree.insert(clause({~p(f2(x1,x2)), ~p(f2(x2,f2(x1,x3))), p(x3)}));
  wtree.insert(clause({~p(x1), ~p(x2), ~p(x3), p(f2(x1,f2(x3,x2)))}));
  wtree.insert(clause({~p(x1), ~p(x2), ~p(f2(x3,x2)), p(f2(x1,x3))}));
  wtree.insert(clause({~p(f2(x1,f2(x2,f2(x3,f2(x1,x4))))), p(x4), ~p(f2(x3,x2))}));
  wtree.insert(clause({~p(f2(x1,f2(x2,f2(x1,x3)))), p(x2), ~p(x3)}));
  wtree.insert(clause({~p(f2(x1,x2)), p(f2(x2,x3)), ~p(x1), ~p(x3)}));
  wtree.insert(clause({~p(f2(f2(x1,f2(x2,x3)),x3)), p(f2(x2,x1))}));
  wtree.insert(clause({~p(x1), ~p(x2), p(f2(x2,f2(x3,f2(x4,x1)))), ~p(f2(x4,x3))}));

  Kernel::Clause* result = clause({~p(f2(x1,f2(x2,x3))), ~p(x4), p(f2(x4,x2)), ~p(x1), ~p(x3)});
  wtree.insert(result);

  wtree.insert(clause({~p(f2(f2(x1,f2(x2,x3)),f2(x2,x1))), ~p(x4), p(f2(x4,x3))}));
  wtree.insert(clause({~p(d)}));
  wtree.insert(clause({~p(x1), ~p(x2), p(f2(x2,x1))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(x4), p(f2(x3,f2(x2,f2(x4,x1))))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(f2(x3,f2(x2,f2(x4,x1)))), ~p(x5), p(f2(x5,x4))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(f2(x2,f2(x4,x1))), p(f2(x3,x4))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(f2(x3,x4)), p(f2(x2,f2(x4,x1)))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(x3), p(f2(x2,x1))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x3)), ~p(f2(x2,x1)), p(x3)}));
  wtree.insert(clause({~p(f2(f2(c,f2(d,e)),e))}));
  wtree.insert(clause({~p(f2(d,c))}));
  wtree.insert(clause({~p(e)}));
  wtree.insert(clause({p(f2(x1,x2)), ~p(x3), ~p(x4), ~p(f2(x2,f2(x1,f2(x4,x3))))}));
  wtree.insert(clause({p(f2(x1,x2)), ~p(f2(x3,f2(x4,x1))), ~p(x2), ~p(f2(x4,x3))}));
  wtree.insert(clause({p(f2(f2(x1,x2),x3)), ~p(f2(x2,f2(x1,x4))), ~p(x3), ~p(x4)}));
  wtree.insert(clause({p(f2(f2(x1,f2(x2,f2(x3,x4))),x5)), ~p(x3), ~p(x5), ~p(x4), ~p(f2(x2,x1))}));
  wtree.insert(clause({p(f2(f2(x1,f2(x2,x3)),x4)), ~p(x2), ~p(x4), ~p(x1), ~p(x3)}));
  wtree.insert(clause({p(f2(f2(f2(x1,f2(x2,x3)),f2(x2,x1)),x4)), ~p(x3), ~p(x4)}));
  wtree.insert(clause({p(f2(f2(x1,x2),x3)), ~p(x1), ~p(x2), ~p(x3)}));
  wtree.insert(clause({p(x1), ~p(x2), ~p(x3), ~p(f2(x1,f2(x3,x2)))}));
  wtree.insert(clause({p(f2(x1,f2(x2,x2))), ~p(x1)}));
  wtree.insert(clause({p(x1), ~p(f2(x2,x3)), ~p(x4), ~p(f2(x3,f2(x2,f2(x4,x1))))}));
  wtree.insert(clause({p(x1), ~p(f2(x2,x3)), ~p(f2(x2,f2(x4,x1))), ~p(x3), ~p(x4)}));
  wtree.insert(clause({p(x1), ~p(f2(x2,f2(f2(x3,x1),f2(x2,x3))))}));
  wtree.insert(clause({~p(f2(x1,f2(x2,f2(x3,x3)))), p(f2(x2,x1))}));
  wtree.insert(clause({~p(x1), p(x2), ~p(f2(x3,f2(x3,f2(x1,x2))))}));
  wtree.insert(clause({~p(x1), ~p(f2(x2,x1)), p(x2)}));

  Kernel::Clause* C1 = clause({~p(x3)});
  wtree.insert(C1);
  wtree.remove(C1);

  wtree.insert(clause({p(f2(x2,x2))}));
  wtree.insert(clause({p(f2(f2(x2,x2),x3)), ~p(x3)}));
  wtree.insert(clause({~p(x1), p(f2(x2,f2(x2,x1)))}));
  wtree.insert(clause({~p(f2(x1,f2(x1,x2))), p(x2)}));
  wtree.insert(clause({~p(x1), p(f2(x1,x2)), ~p(f2(x3,f2(x4,x2))), ~p(f2(x4,x3))}));
  wtree.insert(clause({~p(x1), p(f2(x1,x2)), ~p(f2(x3,f2(x3,x2)))}));
  wtree.insert(clause({~p(x1), p(f2(x1,x2)), ~p(x3), ~p(x4), ~p(f2(x3,f2(x4,x2)))}));
  wtree.insert(clause({~p(f2(x1,x2)), p(f2(x2,f2(x1,x3))), ~p(x3)}));
  wtree.insert(clause({~p(f2(x1,f2(x2,f2(x3,x4)))), p(x3), ~p(x4), ~p(f2(x2,x1))}));
  wtree.insert(clause({~p(f2(x1,f2(x2,x3))), p(x2), ~p(x1), ~p(x3)}));
  wtree.insert(clause({~p(f2(f2(x1,f2(x2,x3)),f2(x2,x1))), p(x3)}));

  Kernel::Clause* D = clause({ ~p(x1), p(f2(x1,x2)), ~p(f2(x3,f2(x2,x4))), ~p(x3), ~p(x4)});
  
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&wtree, D, true);
  int resolvedQueryLit;
  Kernel::Clause* matched_clause = m.next(resolvedQueryLit);
  ASS_EQ(matched_clause, result);
  ASS_EQ(resolvedQueryLit, -1);
}

TEST_FUN(CheckCorrectBindingsPropagation)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  

  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* result = clause({ ~p(f2(x1,f2(x2,x3))), ~p(x4), ~p(x1), ~p(x3) });
  wtree.insert(result);

  Kernel::Clause* D = clause({ ~p(x1), p(f2(x1,x2)), ~p(f2(x3,f2(x2,x4))), ~p(x3), ~p(x4)});

  int resolvedQueryLit;
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&wtree, D, true);
  Kernel::Clause* matched_clause = m.next(resolvedQueryLit);
  ASS_EQ(matched_clause, result);
  ASS_EQ(resolvedQueryLit, -1);
}

TEST_FUN(BigReproducer)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);

  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({~p(f2(x2,x1)), ~p(x2), p(x1)}));
  clauses.push_back(clause({~p(f2(f2(f2(c,f2(d,e)),e),f2(d,c)))}));
  clauses.push_back(clause({p(f2(x1,f2(f2(x2,f2(x3,x1)),f2(x3,x2))))}));
  clauses.push_back(clause({p(f2(f2(x2,f2(x3,x1)),f2(x3,x2))), ~p(x1)}));
  clauses.push_back(clause({~p(f2(x2,f2(x3,x1))), ~p(x1), p(f2(x3,x2))}));
  clauses.push_back(clause({p(f2(f2(x2,f2(x1,x3)),x3)), ~p(f2(x1,x2))}));
  clauses.push_back(clause({p(f2(x2,f2(x1,f2(x2,x3)))), ~p(x1), ~p(x3)}));

  Kernel::Clause* D = clause({~p(x1), ~p(x2), ~p(f2(x3,x2)), p(f2(x1,x3))});

  OptimizedClauseCodeTree<false> optimized_tree;
  for (Kernel::Clause* C : clauses) {
    optimized_tree.insert(C);
  }
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher optimized_m;
  optimized_m.init(&optimized_tree, D, true);
  int resolvedQueryLit;
  optimized_m.next(resolvedQueryLit);

  ClauseCodeTree<false> tree;
  for (Kernel::Clause* C : clauses) {
    tree.insert(C);
  }
  ClauseCodeTree<false>::ClauseMatcher m;
  m.init(&tree, D, true);
  m.next(resolvedQueryLit);
  ASS(optimized_m.countCheckCandidate <= m.countCheckCandidate);
}

TEST_FUN(BigReproducerMinimized)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  std::vector<Kernel::Clause*> clauses;
  clauses.push_back(clause({ ~p(f2(x2,x1)), ~p(x2), p(x1) }));
  Kernel::Clause* D = clause({ ~p(x1), ~p(x2), ~p(f2(x3,x2)), p(f2(x1,x3)) });

  OptimizedClauseCodeTree<false> optimized_tree;
  for (Kernel::Clause* C : clauses) {
    optimized_tree.insert(C);
  }
  std::cout << "Optimized Tree:" << std::endl << optimized_tree << std::endl;
  ClauseCodeTree<false> tree;
  for (Kernel::Clause* C : clauses) {
    tree.insert(C);
  }
  std::cout << "Normal Tree:" << std::endl << tree << std::endl;

  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher optimized_m;
  optimized_m.init(&optimized_tree, D, true);
  int resolvedQueryLit;
  optimized_m.next(resolvedQueryLit);

  ClauseCodeTree<false>::ClauseMatcher m;
  m.init(&tree, D, true);
  m.next(resolvedQueryLit);

  std::cout << "Optimized checkCandidate: " << optimized_m.countCheckCandidate << std::endl;
  std::cout << "Normal checkCandidate: " << m.countCheckCandidate << std::endl;
  ASS(optimized_m.countCheckCandidate <= m.countCheckCandidate);
}

TEST_FUN(EarlyStoppagesDueToDepth)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  wtree.insert(clause({ p(f2(f2(x1,x1),x2)) }));
  wtree.insert(clause({ ~p(x3), ~p(x5), ~p(x4), ~p(f2(x2,x1)) }));
  std::cout << wtree << std::endl;

  Kernel::Clause* D = clause({ ~p(f2(x1,f2(x1,x2))), p(f2(x3,x3)) });
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&wtree, D, true);
  int resolvedQueryLit;
  m.next(resolvedQueryLit);

}
TEST_FUN(OverlapConflicts)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* C1 = clause({p3(c,x1,d), p3(c,d,x1)});
  Kernel::Clause* D = clause({p3(c,e,d), p3(c,d,c)});
  wtree.insert(C1);
  std::cout << wtree << std::endl;
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&wtree, D, true);
  int resolvedQueryLit;
  Kernel::Clause* premise = m.next(resolvedQueryLit);
  ASS_EQ(premise, nullptr);
}

TEST_FUN(EarlyOverlaps)
{
  __ALLOW_UNUSED(SYNTAX_SUGAR_SUBSUMPTION_RESOLUTION);
  OptimizedClauseCodeTree<false> wtree;
  Kernel::Clause* C1 = clause({p3(c,c,c)});
  Kernel::Clause* C2 = clause({p3(c,d,d), p3(e,e,e)});
  Kernel::Clause* C3 = clause({p3(c,d,d), p3(c,c,d) });
  wtree.insert(C1);
  wtree.insert(C2);
  std::cout << wtree << std::endl;

  wtree.insert(C3);
  std::cout << wtree << std::endl;
  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  Kernel::Clause* D = clause({p3(c,c,d), p3(c,d,d)  });
  m.init(&wtree, D, true);
  int resolvedQueryLit;
  Kernel::Clause* premise = m.next(resolvedQueryLit);
  ASS_EQ(premise, C3);
  ASS_EQ(resolvedQueryLit, -1);
}
