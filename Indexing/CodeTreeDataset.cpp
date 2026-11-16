/* See https://vprover.github.io/license.html for the Vampire licence. */
#include "CodeTreeDataset.hpp"
#include "CodeTreeTimeSampler.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/OperatorType.hpp"
#include "Kernel/Signature.hpp"
#include "Lib/Environment.hpp"
#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include <map>
#include <time.h>
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
  struct Sample { uint64_t id; std::string record; };
  fs::path dir;
  std::ofstream journal;
  std::unordered_map<Term*, uint64_t> terms;
  std::unordered_map<Clause*, uint64_t> active;
  std::vector<std::shared_ptr<Sample>> slots;
  unsigned types = 0, functions = 0, predicates = 1;
  unsigned capacity, seed;
  uint64_t mutations = 0, queries = 0, nextClause = 0;
  CodeTreeTimeSampler sampler;
  uint64_t zeroTimes = 0;

  explicit Impl(fs::path path)
    : dir(std::move(path)), capacity(setting("VAMPIRE_CT_SAMPLES", 256)),
      seed(setting("VAMPIRE_CT_SEED", 1)), sampler(capacity, seed)
  {
    slots.resize(capacity);
    if (!fs::create_directory(dir)) throw std::runtime_error("Dataset already exists: " + dir.string());
    journal.exceptions(std::ios::badbit | std::ios::failbit);
    journal.open(dir / "history.ctd", std::ios::binary);
    std::ofstream meta(dir / "metadata.txt");
    meta.exceptions(std::ios::badbit | std::ios::failbit);
    auto sa = env.options->saturationAlgorithm();
    if (sa != Shell::Options::SaturationAlgorithm::OTTER &&
        sa != Shell::Options::SaturationAlgorithm::DISCOUNT)
      throw std::runtime_error("Code tree recording requires -sa otter or -sa discount");
    if (env.higherOrder()) throw std::runtime_error("Code tree dataset v2 supports first-order clauses only");
    meta << "CTD 2\nproblem " << std::quoted(env.options->inputFile())
         << "\nalgorithm " << (sa == Shell::Options::SaturationAlgorithm::OTTER ? "otter" : "discount")
         << "\nversion " << std::quoted(std::string(VERSION_STRING))
         << "\nstrategy " << std::quoted(env.options->generateEncodedOptions())
         << "\nseed " << seed << "\ncapacity " << capacity << '\n';
    timespec resolution;
    if (clock_getres(CLOCK_THREAD_CPUTIME_ID, &resolution))
      throw std::runtime_error("Thread CPU clock unavailable");
    meta << "sampling time_pps_with_replacement\nclock thread_cpu\n"
         << "clock_resolution_ns " << resolution.tv_sec * 1000000000ULL + resolution.tv_nsec
         << "\ntime_floor_ns 1\nmeasurement matcher_init_next_reset\n"
         << "capture_implementation normal\n";
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
    if (env.higherOrder()) throw std::runtime_error("Code tree dataset v2 supports first-order clauses only");
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
    out << "CTD 2 " << journal.tellp() << ' ' << queries << ' '
        << sampler.total() << ' ' << capacity << ' ' << zeroTimes << '\n';
    std::map<uint64_t, std::pair<std::shared_ptr<Sample>, unsigned>> retained;
    for (auto& slot : slots) if (slot) {
      auto& entry = retained[slot->id];
      entry.first = slot;
      ++entry.second;
    }
    for (auto& [id, entry] : retained)
      out << "Q " << id << ' ' << entry.second << ' ' << entry.first->record << '\n';
    out.close();
    fs::rename(dir / "sample.tmp", dir / "sample.ctd");
  }
};

std::unique_ptr<CodeTreeDataset> CodeTreeDataset::create()
{
  const char* output = std::getenv("VAMPIRE_CT_DATASET");
  if (!output || !*output) return nullptr;
#if defined(CT_ANALYSIS) || defined(CT_PROFILE) || VTIME_PROFILING
  std::cerr << "Dataset capture requires CT_ANALYSIS=OFF, CT_PROFILE=OFF and TIME_PROFILING=OFF\n";
  throw std::runtime_error("Dataset capture requires CT_ANALYSIS=OFF, CT_PROFILE=OFF and TIME_PROFILING=OFF");
#endif
  if (std::getenv("VAMPIRE_CT_SAMPLES_PER_BUCKET"))
    throw std::runtime_error("Bucket sampling was replaced: use VAMPIRE_CT_SAMPLES for the total draw count");
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

uint64_t CodeTreeDataset::cpuTimeNs()
{
  timespec now;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now))
    throw std::runtime_error("Cannot read thread CPU clock");
  return uint64_t(now.tv_sec) * 1000000000ULL + now.tv_nsec;
}

void CodeTreeDataset::query(Clause* cl, bool sres, uint64_t cpuNs)
{
  auto& d = *_impl;
  ++d.queries;
  // Give clock-quantized zero durations positive support; expose their count.
  if (!cpuNs) { ++d.zeroTimes; cpuNs = 1; }
  auto replacements = d.sampler.offer(cpuNs);
  if (!replacements.empty()) {
    std::ostringstream out;
    out << d.mutations << ' ' << sres << ' ' << d.active.size() << ' '
        << cpuNs << ' ' << d.clause(cl);
    auto saved = std::make_shared<Impl::Sample>(Impl::Sample{d.queries, out.str()});
    for (auto slot : replacements) d.slots[slot] = saved;
    d.checkpoint();
  } else if (d.queries % 1024 == 0) {
    d.checkpoint();
  }
}
}
