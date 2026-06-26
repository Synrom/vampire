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

  OptimizedClauseCodeTree<false>::OptimizedClauseMatcher m;
  m.init(&ctree, query, sres);
  int resolvedQueryLit;
  Kernel::Clause* matched_clause;
  while ((matched_clause = m.next(resolvedQueryLit))) {
    optimized_results.push_back(std::make_pair(matched_clause, resolvedQueryLit));
  }
  //std::cout << "Tree is:" << std::endl << ctree << std::endl;

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

// TODO: tests to add
/*
 * - add invariant testers
 */

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
