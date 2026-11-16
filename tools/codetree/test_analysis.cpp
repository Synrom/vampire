/* See https://vprover.github.io/license.html for the Vampire licence. */
#include "Indexing/CodeTreeAnalysis.hpp"
#include <iostream>
#include <stdexcept>
using namespace Indexing;
void check(bool ok) { if (!ok) throw std::runtime_error("Analysis assertion failed"); }
int main() {
  using CT = CodeTree;
  CT tree;
  tree._entryPoint = CT::CodeBlock::allocate(2);
  auto root = tree.getEntryPoint();
  root[0] = CT::CodeOp::getTermOp(CT::CHECK_FUN, 0);
  // op1 -> op2 -> op3 -> op4 -> op5: four choices after root.
  auto a = CT::CodeBlock::allocate(1);
  auto b = CT::CodeBlock::allocate(1);
  auto c = CT::CodeBlock::allocate(1);
  root[1].setAlternative(&(*a)[0]);
  (*a)[0].setAlternative(&(*b)[0]);
  (*b)[0].setAlternative(&(*c)[0]);
  {
    CodeTreeAnalysis analysis(tree);
    check(analysis.nodes.size() == 5);
    check(analysis.nodes.at(root).branches == 4);
    check(analysis.nodes.at(root+1).branches == 1);
    check(analysis.maxDepth == -1);
    analysis.hit(root);
    check(analysis.expanded == 1 && analysis.nodes.at(root).hits == 1);
  }
  // Replace two alternatives with a compressed search, retaining a null hole.
  struct TestSearch : CT::SearchStruct {
    TestSearch() : CT::SearchStruct(FN_STRUCT, 3) {}
  };
  auto ss = new TestSearch;
  ss->targets = {&(*a)[0], nullptr, &(*b)[0]};
  (*a)[0].setAlternative(nullptr);
  (*b)[0].setAlternative(nullptr);
  ss->landingOp.setAlternative(&(*c)[0]);
  root[1].setAlternative(&ss->landingOp);
  {
    CodeTreeAnalysis analysis(tree);
    check(analysis.nodes.size() == 5); // no double counting search target heads
    check(analysis.nodes.at(root).branches == 4);
    analysis.hit(&ss->landingOp);
    check(analysis.dispatches == 1 && analysis.expanded == 2);
    analysis.hit(&(*a)[0]);
    check(analysis.nodes.at(&(*a)[0]).hits == 2);
    check(analysis.nodes.at(&(*b)[0]).hits == 1);
    check(analysis.nodes.at(&(*c)[0]).hits == 0);
  }
  Lib::Stack<unsigned> vars;
  CT::ILStruct ils(nullptr, 0, vars);
  ils.depth = 7;
  root[0] = CT::CodeOp::getLitEnd(&ils);
  {
    CodeTreeAnalysis analysis(tree);
    analysis.hit(root);
    check(analysis.maxDepth == 7);
  }
  root[0].makeFail();
  // Restore ownership to the ordinary blocks before destroying the test-only
  // SearchStruct subclass (production destroy() expects SearchStructImpl).
  root[1].setAlternative(&(*a)[0]);
  (*a)[0].setAlternative(&(*b)[0]);
  (*b)[0].setAlternative(&(*c)[0]);
  delete ss;
  std::cout << "Passed: alternative chains, search targets/holes, terminals, hotness, depth\n";
}
