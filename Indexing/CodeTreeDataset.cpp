/* See https://vprover.github.io/license.html for the Vampire licence. */
#include "CodeTreeDataset.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/OperatorType.hpp"
#include "Kernel/Signature.hpp"
#include "Lib/Environment.hpp"
#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Indexing {
using namespace Kernel;
using namespace Lib;
namespace fs = std::filesystem;

namespace {
unsigned setting(const char* name, unsigned fallback)
{
  const char* s = std::getenv(name);
  if (!s) return fallback;
  std::string value(s);
  size_t end;
  auto n = std::stoul(value, &end);
  if (end != value.size() || n == 0 || n > 1000000)
    throw std::runtime_error(std::string("Invalid dataset setting: ") + name);
  return static_cast<unsigned>(n);
}
}

struct CodeTreeDataset::Impl {
  struct Bucket {
    uint64_t seen = 0;
    std::vector<std::string> queries;
  };
  fs::path dir;
  std::ofstream journal;
  std::unordered_map<Term*, uint64_t> terms;
  std::unordered_map<Clause*, uint64_t> active;
  std::array<Bucket, 64> buckets;
  unsigned types = 0, functions = 0, predicates = 1;
  unsigned capacity, seed;
  uint64_t mutations = 0, queries = 0, nextClause = 0;
  std::mt19937_64 rng;

  explicit Impl(fs::path path)
    : dir(std::move(path)), capacity(setting("VAMPIRE_CT_SAMPLES_PER_BUCKET", 4)),
      seed(setting("VAMPIRE_CT_SEED", 1)), rng(seed)
  {
    if (!fs::create_directory(dir)) throw std::runtime_error("Dataset already exists: " + dir.string());
    journal.exceptions(std::ios::badbit | std::ios::failbit);
    journal.open(dir / "history.ctd", std::ios::binary);
    std::ofstream meta(dir / "metadata.txt");
    meta.exceptions(std::ios::badbit | std::ios::failbit);
    auto sa = env.options->saturationAlgorithm();
    if (sa != Shell::Options::SaturationAlgorithm::OTTER &&
        sa != Shell::Options::SaturationAlgorithm::DISCOUNT)
      throw std::runtime_error("Code tree recording requires -sa otter or -sa discount");
    if (env.higherOrder()) throw std::runtime_error("Code tree dataset v1 supports first-order clauses only");
    meta << "CTD 1\nproblem " << std::quoted(env.options->inputFile())
         << "\nalgorithm " << (sa == Shell::Options::SaturationAlgorithm::OTTER ? "otter" : "discount")
         << "\nversion " << std::quoted(std::string(VERSION_STRING))
         << "\nstrategy " << std::quoted(env.options->generateEncodedOptions())
         << "\nseed " << seed << "\ncapacity " << capacity << '\n';
    // FNV-1a is an identity aid, not a cryptographic checksum.
    std::ifstream input(env.options->inputFile(), std::ios::binary);
    bool inputAvailable = bool(input);
    uint64_t hash = 14695981039346656037ULL;
    char ch;
    while (input.get(ch)) { hash ^= static_cast<unsigned char>(ch); hash *= 1099511628211ULL; }
    meta << "input_fnv1a64 ";
    if (inputAvailable) meta << std::hex << hash;
    else meta << "unavailable";
    meta << '\n';
    checkpoint();
  }

  // Definitions precede references. Types, terms and literals share a DAG table;
  // variable numbers and original shared IDs are preserved, including equality
  // orientation. No printed-TPTP round trip or preprocessing is involved.
  std::string term(TermList value)
  {
    if (value.isVar()) return std::string(value.isSpecialVar() ? "x" : "v") + std::to_string(value.var());
    if (value == AtomicSort::superSort()) return "u";
    Term* t = value.term();
    auto found = terms.find(t);
    if (found != terms.end()) return "t" + std::to_string(found->second);
    if (t->isSpecial() || !t->shared()) throw std::runtime_error("Unsupported special/non-shared dataset term");
    std::vector<std::string> args;
    for (unsigned i = 0; i < t->arity(); ++i) args.push_back(term(*t->nthArgument(i)));
    std::string eqSort = "u";
    if (t->isLiteral() && static_cast<Literal*>(t)->isEquality())
      eqSort = term(static_cast<Literal*>(t)->eqArgSort());
    auto id = terms.size() + 1;
    terms.emplace(t, id);
    journal << "T " << id << ' ' << t->getId() << ' '
            << (t->isSort() ? 'S' : t->isLiteral() ? 'L' : 'F') << ' '
            << t->functor() << ' ' << t->arity() << ' '
            << (t->isLiteral() ? static_cast<Literal*>(t)->polarity() : false) << ' ' << eqSort;
    for (const auto& a : args) journal << ' ' << a;
    journal << '\n';
    return "t" + std::to_string(id);
  }

  void signature()
  {
    if (env.higherOrder()) throw std::runtime_error("Code tree dataset v1 supports first-order clauses only");
    // Preserve symbol numbers, not just names: CHECK_FUN search structures use them.
    while (types < env.signature->typeCons()) {
      auto s = env.signature->getTypeCon(types);
      journal << "S " << types++ << ' ' << s->arity() << ' ' << std::quoted(s->name()) << '\n';
    }
    auto symbols = [&](bool predicate, unsigned& cursor, unsigned count) {
      while (cursor < count) {
        auto s = predicate ? env.signature->getPredicate(cursor) : env.signature->getFunction(cursor);
        auto ty = s->type();
        std::vector<std::string> args;
        for (unsigned i = ty->numTypeArguments(); i < ty->arity(); ++i) args.push_back(term(ty->arg(i)));
        auto result = predicate ? "u" : term(ty->result());
        journal << (predicate ? 'P' : 'F') << ' ' << cursor++ << ' '
                << ty->numTypeArguments() << ' ' << args.size() << ' ' << std::quoted(s->name()) << ' ' << result;
        for (const auto& a : args) journal << ' ' << a;
        journal << '\n';
      }
    };
    symbols(false, functions, env.signature->functions());
    symbols(true, predicates, env.signature->predicates());
  }

  std::string clause(Clause* cl)
  {
    signature();
    std::ostringstream out;
    out << cl->length();
    for (unsigned i = 0; i < cl->length(); ++i) out << ' ' << term(TermList((*cl)[i]));
    return out.str();
  }

  void checkpoint()
  {
    journal.flush();
    std::ofstream out(dir / "sample.tmp", std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    // Only this durable prefix is needed by the sample. Later records may be
    // incomplete after _Exit from Vampire's timer thread.
    out << "CTD 1 " << journal.tellp() << ' ' << queries << '\n';
    for (unsigned i = 0; i < buckets.size(); ++i) {
      const auto& b = buckets[i];
      out << "B " << i << ' ' << b.seen << ' ' << b.queries.size() << '\n';
      for (const auto& q : b.queries) out << q << '\n';
    }
    out.close();
    fs::rename(dir / "sample.tmp", dir / "sample.ctd");
  }
};

std::unique_ptr<CodeTreeDataset> CodeTreeDataset::create()
{
  const char* output = std::getenv("VAMPIRE_CT_DATASET");
  if (!output || !*output) return nullptr;
  static unsigned nextTree = 0;
  // Each process must use its own destination (single-strategy runs).
  static fs::path root = [&]() {
    fs::path p(output);
    if (!fs::create_directory(p)) throw std::runtime_error("Dataset destination must be new: " + p.string());
    return p;
  }();
  return std::unique_ptr<CodeTreeDataset>(new CodeTreeDataset(
    std::make_unique<Impl>(root / ("tree-" + std::to_string(nextTree++)))));
}

CodeTreeDataset::CodeTreeDataset(std::unique_ptr<Impl> impl) : _impl(std::move(impl)) {}
CodeTreeDataset::~CodeTreeDataset()
{
  try { _impl->checkpoint(); }
  catch (const std::exception& e) { std::cerr << "Dataset checkpoint failed: " << e.what() << '\n'; }
}

void CodeTreeDataset::insert(Clause* cl, Literal* const* ordered)
{
  auto& d = *_impl;
  auto serialized = d.clause(cl);
  auto id = ++d.nextClause;
  if (!d.active.emplace(cl, id).second) throw std::runtime_error("Duplicate dataset insertion");
  d.journal << "I " << ++d.mutations << ' ' << id << ' ' << serialized;
  std::vector<bool> used(cl->length(), false);
  for (unsigned i = 0; i < cl->length(); ++i) {
    unsigned j = 0;
    while (j < cl->length() && (used[j] || (*cl)[j] != ordered[i])) ++j;
    if (j == cl->length()) throw std::runtime_error("Invalid insertion permutation");
    used[j] = true;
    d.journal << ' ' << j;
  }
  d.journal << '\n';
}

void CodeTreeDataset::remove(Clause* cl)
{
  auto& d = *_impl;
  auto id = d.active.at(cl);
  d.active.erase(cl);
  d.journal << "R " << ++d.mutations << ' ' << id << '\n';
}

void CodeTreeDataset::query(Clause* cl, bool sres)
{
  auto& d = *_impl;
  ++d.queries;
  unsigned size = d.active.size() < 32 ? 0 : d.active.size() < 256 ? 1 : d.active.size() < 2048 ? 2 : 3;
  unsigned length = cl->length() <= 1 ? 0 : cl->length() == 2 ? 1 : cl->length() <= 4 ? 2 : 3;
  unsigned phase = d.queries <= 256 ? 0 : d.queries <= 4096 ? 1 : d.queries <= 65536 ? 2 : 3;
  unsigned bucket = (phase * 4 + size) * 4 + length;
  auto& b = d.buckets[bucket];
  ++b.seen;
  uint64_t slot = b.queries.size() < d.capacity ? b.queries.size() : std::uniform_int_distribution<uint64_t>(0, b.seen - 1)(d.rng);
  if (slot < d.capacity) {
    std::ostringstream out;
    out << "Q " << d.queries << ' ' << d.mutations << ' ' << bucket << ' ' << sres << ' '
        << d.active.size() << ' ' << d.clause(cl);
    if (slot == b.queries.size()) b.queries.push_back(out.str());
    else b.queries[slot] = out.str();
    d.checkpoint();
  } else if (d.queries % 1024 == 0) {
    d.checkpoint();
  }
}
}
