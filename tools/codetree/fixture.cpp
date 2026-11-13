/* See https://vprover.github.io/license.html for the Vampire licence. */
// Integration-test producer: expected results are a separate test oracle file,
// never part of the captured dataset.
#include "Indexing/ClauseCodeTree.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/OperatorType.hpp"
#include "Kernel/Signature.hpp"
#include "Lib/Environment.hpp"
#include "Shell/Options.hpp"
#include <fstream>
#include <iostream>
#include <vector>

using namespace Kernel;
using namespace Lib;
using namespace Indexing;

int main(int argc, char** argv)
{
  if (argc != 3) { std::cerr << "Usage: fixture DATASET EXPECTED.csv\n"; return 1; }
  setenv("VAMPIRE_CT_DATASET", argv[1], 1);
  env.options->setSaturationAlgorithm(Shell::Options::SaturationAlgorithm::OTTER);
  env.options->setInputFile("synthetic-fixture");
  auto sort = TermList(AtomicSort::createConstant(env.signature->addTypeCon("fixture_sort", 0)->number()));
  auto a = TermList(Term::createConstant(env.signature->addFunction("a", OperatorType::getConstantsType(sort))->number()));
  auto b = TermList(Term::createConstant(env.signature->addFunction("b", OperatorType::getConstantsType(sort))->number()));
  auto f = env.signature->addFunction("f", OperatorType::getFunctionType({sort}, sort))->number();
  auto p = env.signature->addPredicate("p", OperatorType::getPredicateType({sort}))->number();
  auto q = env.signature->addPredicate("q", OperatorType::getPredicateType({sort}))->number();
  TermList x(0, false), y(1, false);
  auto fx = TermList(Term::create(f, {x}));
  auto fa = TermList(Term::create(f, {a}));
  // Polymorphic predicate and parametric sort exercise type arguments as data.
  auto box = env.signature->addTypeCon("box", 1)->number();
  auto boxed = TermList(AtomicSort::create(box, 1, &sort));
  auto c = TermList(Term::createConstant(env.signature->addFunction("c", OperatorType::getConstantsType(boxed))->number()));
  auto poly = env.signature->addPredicate("poly", OperatorType::getPredicateType({x}, 1))->number();
  std::vector<Clause*> owned;
  auto clause = [&](std::initializer_list<Literal*> lits) {
    auto cl = Clause::fromLiterals(lits, FromInput(UnitInputType::AXIOM));
    cl->incRefCnt(); owned.push_back(cl); return cl;
  };
  auto c1 = clause({Literal::create(p, true, {x})});
  auto c2 = clause({Literal::create(q, false, {x}), Literal::create(p, true, {fx})});
  auto c3 = clause({Literal::createEquality(true, x, y, sort)});
  auto c4 = clause({Literal::create(poly, true, {boxed, x})});
  // SAT set-resolution can collapse p(X),p(Y); code trees require distinct
  // query literals. This guards the completeness oracle's multiset semantics.
  auto c5 = clause({Literal::create(p, true, {x}), Literal::create(p, true, {y}), Literal::create(q, true, {b})});
  std::vector<Clause*> queries{
    clause({Literal::create(q, false, {a}), Literal::create(p, true, {fa})}),
    clause({Literal::create(p, false, {a})}),
    clause({Literal::createEquality(true, b, fa, sort)}),
    clause({Literal::create(q, true, {a})}),
    clause({Literal::create(poly, true, {boxed, c})}),
    clause({Literal::create(p, true, {a}), Literal::create(q, false, {b})})
  };
  std::ofstream expected(argv[2]);
  expected << "query,matches\n";
  {
    ClauseCodeTree tree;
    tree.insert(c1); tree.insert(c2); tree.insert(c3); tree.insert(c4); tree.insert(c5);
    for (unsigned i = 1; i <= 5000; ++i) {
      if (i % 100 == 0) tree.remove(c1);
      if (i % 100 == 50) { tree.remove(c2); tree.insert(c2); }
      auto query = queries[i % queries.size()];
      ClauseCodeTree::ClauseMatcher matcher;
      matcher.init(&tree, query, i % 2 == 0);
      int resolved;
      unsigned matches = 0;
      while (matcher.next(resolved)) ++matches;
      matcher.reset();
      expected << i << ',' << matches << '\n';
      if (i % 100 == 0) tree.insert(c1);
    }
  }
  for (auto cl : owned) cl->decRefCnt();
}
