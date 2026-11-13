/* See https://vprover.github.io/license.html for the Vampire licence. */
#include "Indexing/ClauseCodeTree.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/OperatorType.hpp"
#include "Kernel/Signature.hpp"
#include "Lib/Environment.hpp"
#include "SATSubsumption/SATSubsumptionAndResolution.hpp"
#include "Shell/Options.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using namespace Kernel;
using namespace Lib;
using namespace Indexing;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
void require(bool ok, const std::string& message)
{ if (!ok) throw std::runtime_error(message); }

uint64_t number(const std::string& text)
{
  require(!text.empty() && text[0] >= '0' && text[0] <= '9', "Invalid integer: " + text);
  size_t pos;
  auto n = std::stoull(text, &pos);
  require(pos == text.size(), "Invalid integer: " + text);
  return n;
}

std::string csv(const std::string& value)
{
  std::string out = "\"";
  for (char c : value) { out += c; if (c == '"') out += c; }
  return out + '"';
}

struct Query {
  uint64_t id, mutation, size;
  unsigned bucket;
  bool sres;
  Clause* clause;
};
struct Mutation {
  bool insert;
  uint64_t id;
  Clause* clause;
  std::vector<unsigned> order;
};

struct Dataset {
  std::unordered_map<uint64_t, Term*> terms;
  std::vector<Clause*> owned;
  std::vector<Mutation> mutations;
  std::vector<Query> queries;
  std::map<unsigned, std::pair<uint64_t, uint64_t>> buckets;
  std::string problem, algorithm;
  uint64_t population = 0;

  ~Dataset() { for (auto cl : owned) cl->decRefCnt(); }

  TermList term(std::istream& in)
  {
    std::string s;
    require(bool(in >> s), "Missing term reference");
    if (s == "u") return AtomicSort::superSort();
    require(s.size() >= 2, "Invalid term reference");
    auto n = number(s.substr(1));
    if (s[0] == 'v' || s[0] == 'x') {
      require(n <= UINT32_MAX, "Variable out of range");
      return TermList(n, s[0] == 'x');
    }
    require(s[0] == 't' && terms.count(n), "Unknown term reference: " + s);
    return TermList(terms.at(n));
  }

  Clause* clause(std::istream& in)
  {
    unsigned length;
    require(bool(in >> length) && length < 1000000, "Invalid clause length");
    Stack<Literal*> lits;
    for (unsigned i = 0; i < length; ++i) {
      auto t = term(in);
      require(t.isTerm() && t.term()->isLiteral(), "Clause member is not a literal");
      lits.push(static_cast<Literal*>(t.term()));
    }
    auto cl = Clause::fromStack(lits, FromInput(UnitInputType::AXIOM));
    cl->incRefCnt();
    owned.push_back(cl);
    return cl;
  }

  void load(const fs::path& path)
  {
    std::ifstream meta(path / "metadata.txt");
    std::string line, key;
    require(bool(std::getline(meta, line)) && line == "CTD 1", "Unsupported/missing metadata");
    while (std::getline(meta, line)) {
      std::istringstream in(line);
      in >> key;
      if (key == "problem") in >> std::quoted(problem);
      if (key == "algorithm") in >> algorithm;
    }
    require(algorithm == "otter" || algorithm == "discount", "Invalid saturation algorithm");
    std::ifstream sample(path / "sample.ctd", std::ios::binary);
    unsigned version;
    uint64_t bytes;
    require(bool(sample >> key >> version >> bytes >> population) && key == "CTD" && version == 1, "Invalid sample header");
    require(bytes <= fs::file_size(path / "history.ctd"), "Truncated history");
    std::ifstream history(path / "history.ctd", std::ios::binary);
    std::unordered_map<uint64_t, Clause*> active;
    uint64_t position = 0;
    while (position < bytes && std::getline(history, line)) {
      position += line.size() + 1;
      require(position <= bytes, "Checkpoint splits history record");
      std::istringstream in(line);
      char tag;
      in >> tag;
      if (tag == 'S') {
        unsigned id, arity;
        std::string name;
        require(bool(in >> id >> arity >> std::quoted(name)) && arity < 1000000, "Invalid sort definition");
        if (id < env.signature->typeCons()) {
          require(env.signature->getTypeCon(id)->arity() == arity, "Builtin sort mismatch");
        } else {
          require(id == env.signature->typeCons(), "Nonsequential sort ID");
          require(env.signature->addTypeCon("ctd_sort_" + std::to_string(id), arity)->number() == id, "Sort ID mismatch");
        }
      } else if (tag == 'F' || tag == 'P') {
        unsigned id, ta, arity;
        std::string name;
        require(bool(in >> id >> ta >> arity >> std::quoted(name)) && arity < 1000000 && ta < 1000000, "Invalid symbol definition");
        auto result = term(in);
        Stack<TermList> args;
        for (unsigned i = 0; i < arity; ++i) args.push(term(in));
        unsigned created;
        if (tag == 'F') {
          require(id == env.signature->functions(), "Nonsequential function ID");
          created = env.signature->addFunction("ctd_function_" + std::to_string(id), OperatorType::getFunctionType(args, result, ta))->number();
        } else {
          require(id == env.signature->predicates(), "Nonsequential predicate ID");
          created = env.signature->addPredicate("ctd_predicate_" + std::to_string(id), OperatorType::getPredicateType(args, ta))->number();
        }
        require(created == id, "Symbol ID mismatch");
      } else if (tag == 'T') {
        uint64_t id;
        unsigned sharedId, functor, arity;
        char kind;
        bool polarity;
        require(bool(in >> id >> sharedId >> kind >> functor >> arity >> polarity), "Invalid term header");
        require(arity < 1000000 && !terms.count(id), "Invalid term definition");
        auto sort = term(in);
        std::vector<TermList> args;
        for (unsigned i = 0; i < arity; ++i) args.push_back(term(in));
        Term* t;
        if (kind == 'S') {
          require(functor < env.signature->typeCons() && env.signature->getTypeCon(functor)->arity() == arity, "Sort arity mismatch");
          t = AtomicSort::create(functor, arity, args.data());
        } else if (kind == 'F') {
          require(functor < env.signature->functions() && env.signature->getFunction(functor)->arity() == arity, "Function arity mismatch");
          t = Term::create(functor, arity, args.data());
        } else {
          require(kind == 'L' && functor < env.signature->predicates() && env.signature->getPredicate(functor)->arity() == arity, "Literal arity mismatch");
          t = functor == 0 ? Literal::createEquality(polarity, args.at(0), args.at(1), sort)
                           : Literal::create(functor, arity, polarity, args.data());
          for (unsigned i = 0; i < arity; ++i)
            require(*t->nthArgument(i) == args[i], "Literal orientation changed on import");
        }
        // Assign before creating any parent: equality normalization depends on IDs.
        t->setId(sharedId);
        terms.emplace(id, t);
      } else if (tag == 'I' || tag == 'R') {
        uint64_t event, id;
        require(bool(in >> event >> id), "Invalid mutation header");
        require(event == mutations.size() + 1, "Nonsequential mutation");
        Mutation m{tag == 'I', id, nullptr, {}};
        if (m.insert) {
          m.clause = clause(in);
          require(active.emplace(id, m.clause).second, "Duplicate insertion");
          std::set<unsigned> seen;
          for (unsigned i = 0; i < m.clause->length(); ++i) {
            unsigned j;
            require(bool(in >> j), "Missing insertion permutation");
            require(j < m.clause->length() && seen.insert(j).second, "Invalid insertion permutation");
            m.order.push_back(j);
          }
        } else {
          require(active.count(id), "Removal of absent clause");
          m.clause = active.at(id);
          active.erase(id);
        }
        mutations.push_back(std::move(m));
      } else throw std::runtime_error("Unknown history record");
      require(!in.fail(), "Malformed history record");
      std::string extra;
      require(!(in >> extra), "Extra history fields");
    }
    require(position == bytes, "Incomplete history prefix");
    std::getline(sample, line);
    unsigned currentBucket = 64;
    std::map<unsigned, uint64_t> retained;
    std::set<uint64_t> queryIds;
    while (std::getline(sample, line)) {
      std::istringstream in(line);
      char tag;
      in >> tag;
      if (tag == 'B') {
        uint64_t seen, count;
        require(bool(in >> currentBucket >> seen >> count), "Invalid bucket header");
        require(currentBucket < 64 && count <= seen && !buckets.count(currentBucket), "Invalid bucket");
        buckets[currentBucket] = {seen, count};
      } else if (tag == 'Q') {
        Query q;
        require(bool(in >> q.id >> q.mutation >> q.bucket >> q.sres >> q.size), "Invalid query header");
        require(q.bucket == currentBucket && q.mutation <= mutations.size() && q.id <= population && queryIds.insert(q.id).second, "Invalid query metadata");
        q.clause = clause(in);
        queries.push_back(q);
        ++retained[q.bucket];
      } else throw std::runtime_error("Unknown sample record");
      require(!in.fail(), "Malformed sample record");
      std::string extra;
      require(!(in >> extra), "Extra sample fields");
    }
    uint64_t seen = 0;
    require(buckets.size() == 64, "Missing buckets");
    for (auto [id, b] : buckets) { require(retained[id] == b.second, "Missing retained query"); seen += b.first; }
    require(seen == population, "Population mismatch");
    std::sort(queries.begin(), queries.end(), [](auto& a, auto& b) { return a.id < b.id; });
  }
};

void verify(ClauseCodeTree& tree, const Query& q, const std::map<uint64_t, Clause*>& active)
{
  SATSubsumption::SATSubsumptionAndResolution oracle;
  // ClauseCodeTree uses multiset matching for resolution too (see its TODO in
  // matchGlobalVars). SAT's resolution mode permits set matching, so using it
  // as a completeness oracle would report legitimate differences as bugs.
  // Flipping one query literal and checking *multiset subsumption* implements
  // precisely the code tree's at-most-one-opposite, distinct-query-literals rule.
  using OwnedClause = std::unique_ptr<Clause, void(*)(Clause*)>;
  std::vector<OwnedClause> flipped;
  if (q.sres) {
    for (unsigned i = 0; i < q.clause->length(); ++i) {
      Stack<Literal*> lits;
      for (unsigned j = 0; j < q.clause->length(); ++j)
        lits.push(i == j ? Literal::complementaryLiteral((*q.clause)[j]) : (*q.clause)[j]);
      auto cl = Clause::fromStack(lits, FromInput(UnitInputType::AXIOM));
      cl->incRefCnt();
      flipped.emplace_back(cl, [](Clause* c) { c->decRefCnt(); });
    }
  }
  ClauseCodeTree::ClauseMatcher matcher;
  matcher.init(&tree, q.clause, q.sres);
  std::set<Clause*> found;
  int resolved;
  while (auto cl = matcher.next(resolved)) {
    bool valid = resolved == -1 ? oracle.checkSubsumption(cl, q.clause)
      : q.sres && oracle.checkSubsumptionResolutionWithLiteral(cl, q.clause, resolved);
    require(valid, "Invalid match for query " + std::to_string(q.id));
    found.insert(cl);
  }
  matcher.reset();
  for (auto [id, cl] : active) {
    bool expected = oracle.checkSubsumption(cl, q.clause);
    for (const auto& opposite : flipped) {
      if (expected) break;
      expected = oracle.checkSubsumption(cl, opposite.get());
    }
    require(expected == bool(found.count(cl)), "Match completeness mismatch for query " + std::to_string(q.id) + ", indexed clause " + std::to_string(id)
      + "\nquery: " + q.clause->toString() + "\nindexed: " + cl->toString());
  }
}
}

int main(int argc, char** argv)
{
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout << "Usage: codetree-replay DATASET [--repeat N] [--all] [--verify] [--query ID] [--bucket ID]\n"
                   "                       [--problem SUBSTRING] [--algorithm otter|discount]\n"
                   "DATASET can be a tree directory or a collection of captured runs.\n"
                   "CSV goes to stdout; setup and validation summaries go to stderr.\n";
      return argc < 2 ? 1 : 0;
    }
    // Replay must never record, even if invoked from a capture shell.
    unsetenv("VAMPIRE_CT_DATASET");
    env.options->set("random_traversals", "off");
    uint64_t repeat = 1, queryFilter = 0;
    int bucketFilter = -1;
    bool all = false, check = false, header = true;
    std::string problemFilter, algorithmFilter;
    for (int i = 2; i < argc; ++i) {
      std::string option(argv[i]);
      if (option == "--all") all = true;
      else if (option == "--verify") check = true;
      else if (option == "--no-header") header = false;
      else if (option == "--problem" || option == "--algorithm") {
        require(i + 1 < argc, "Missing option value");
        if (option == "--problem") problemFilter = argv[++i];
        else {
          algorithmFilter = argv[++i];
          require(algorithmFilter == "otter" || algorithmFilter == "discount", "Algorithm must be otter or discount");
        }
      }
      else {
        require(i + 1 < argc, "Missing option value");
        auto value = number(argv[++i]);
        if (option == "--repeat") { require(value > 0, "Repeat must be positive"); repeat = value; }
        else if (option == "--query") { require(value > 0, "Query ID must be positive"); queryFilter = value; }
        else if (option == "--bucket") { require(value < 64, "Bucket must be 0..63"); bucketFilter = value; }
        else throw std::runtime_error("Unknown option: " + option);
      }
    }
    constexpr const char* csvHeader = "dataset,problem,algorithm,query,bucket,population,retained,weight,index_size,query_literals,sres,mode,repeats,matches,total_ns\n";
    if (!fs::exists(fs::path(argv[1]) / "metadata.txt")) {
      std::vector<fs::path> trees;
      for (const auto& entry : fs::recursive_directory_iterator(argv[1])) {
        if (entry.path().filename() != "metadata.txt") continue;
        std::ifstream meta(entry.path());
        std::string line, key, problem, algorithm;
        if (!std::getline(meta, line) || line != "CTD 1") continue;
        while (std::getline(meta, line)) {
          std::istringstream in(line);
          in >> key;
          if (key == "problem") in >> std::quoted(problem);
          if (key == "algorithm") in >> algorithm;
        }
        if (problem.find(problemFilter) != std::string::npos &&
            (algorithmFilter.empty() || algorithm == algorithmFilter))
          trees.push_back(entry.path().parent_path());
      }
      require(!trees.empty(), "No datasets match the problem/algorithm filters");
      std::sort(trees.begin(), trees.end());
      if (header) std::cout << csvHeader;
      std::cout.flush();
      // Vampire has process-global symbol/term sharing. A fresh exec per tree
      // keeps original numeric IDs intact without unsafe global-state resets.
      for (const auto& tree : trees) {
        std::vector<std::string> args{argv[0], tree.string(), "--no-header"};
        for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
        std::vector<char*> raw;
        for (auto& arg : args) raw.push_back(arg.data());
        raw.push_back(nullptr);
        auto child = fork();
        require(child >= 0, "Cannot fork replay worker");
        if (child == 0) {
          execvp(raw[0], raw.data());
          std::cerr << "Cannot execute replay worker\n";
          std::_Exit(1);
        }
        int status;
        require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Replay failed for " + tree.string());
      }
      return 0;
    }
    Dataset data;
    auto start = Clock::now();
    data.load(argv[1]);
    require(data.problem.find(problemFilter) != std::string::npos &&
            (algorithmFilter.empty() || data.algorithm == algorithmFilter), "Dataset does not match the problem/algorithm filters");
    auto seconds = [](auto duration) { return std::chrono::duration<double>(duration).count(); };
    std::cerr << "Loaded " << data.queries.size() << " samples from " << data.population << " queries, "
              << data.mutations.size() << " mutations in " << seconds(Clock::now() - start) << " s\n";
    ClauseCodeTree tree;
    std::map<uint64_t, Clause*> active;
    size_t event = 0, executed = 0;
    double setup = 0, verification = 0;
    if (header) std::cout << csvHeader;
    for (const auto& q : data.queries) {
      if ((queryFilter && q.id != queryFilter) || (bucketFilter >= 0 && q.bucket != unsigned(bucketFilter))) continue;
      start = Clock::now();
      while (event < q.mutation) {
        const auto& m = data.mutations[event++];
        if (m.insert) { tree.insert(m.clause, m.order.data()); active.emplace(m.id, m.clause); }
        else { tree.remove(m.clause); active.erase(m.id); }
      }
      setup += seconds(Clock::now() - start);
      require(active.size() == q.size && !tree.isEmpty(), "Reconstructed index size mismatch");
      if (check) {
        start = Clock::now();
        verify(tree, q, active);
        verification += seconds(Clock::now() - start);
      }
      uint64_t matches = 0;
      ClauseCodeTree::ClauseMatcher matcher;
      start = Clock::now();
      for (uint64_t iteration = 0; iteration < repeat; ++iteration) {
        matcher.init(&tree, q.clause, q.sres);
        int resolved;
        while (matcher.next(resolved)) { ++matches; if (!all) break; }
        matcher.reset();
      }
      auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
      auto [seen, kept] = data.buckets.at(q.bucket);
      std::cout << csv(argv[1]) << ',' << csv(data.problem) << ',' << data.algorithm << ',' << q.id << ',' << q.bucket << ','
                << seen << ',' << kept << ',' << std::setprecision(12) << double(seen) / kept << ','
                << q.size << ',' << q.clause->length() << ',' << q.sres << ',' << (all ? "all" : "first")
                << ',' << repeat << ',' << matches << ',' << ns << '\n';
      ++executed;
    }
    require(executed > 0, "No queries selected");
    std::cerr << "Executed " << executed << " queries; index reconstruction " << setup
              << " s; verification " << verification << " s" << (check ? " (all passed)" : "") << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "codetree-replay: " << e.what() << '\n';
    return 1;
  }
}
