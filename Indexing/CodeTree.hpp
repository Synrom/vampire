/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file CodeTree.hpp
 * Defines class CodeTree.
 */

#ifndef __CodeTree__
#define __CodeTree__

#include <bit>

#include "Forwards.hpp"

#include "Lib/Allocator.hpp"
#include "Lib/DArray.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/FlexibleTail.hpp"
#include "Lib/Stack.hpp"
#include "Lib/Vector.hpp"

#include "Kernel/FlatTerm.hpp"

#define LOG_OP(x)
//#define LOG_OP(x) std::cout<<x<<std::endl
//#define LOG_OP(x) if(TimeCounter::isBeingMeasured(TC_FORWARD_SUBSUMPTION)) { std::cout<<x<<std::endl; }

namespace Indexing {

using namespace Lib;
using namespace Kernel;

class CodeTree
{
public:
  struct ILStruct;
  struct SearchStruct;
  struct CodeOp;
  struct CodeTreeSubtree;

protected:
  /**
  * During the destruction of the CodeTree,
  * onCodeOpDestroying is called on each CodeOp
  * as an opportunity to release dynamically
  * allocated memory "owned" by the particular CodeOp
  * (the details are expected to be descendant specific)
  */
  virtual void onCodeOpDestroying(CodeOp* op) {}
  virtual void printSuccess(std::ostream& out, const CodeOp& op) const {}

public:
  virtual ~CodeTree();

  struct LitInfo
  {
    LitInfo() {}
    LitInfo(Clause* cl, unsigned litIndex);
    void dispose();

    static LitInfo getReversed(const LitInfo& li);
    static LitInfo getOpposite(const LitInfo& li);

    /** Index of this LitInfo in the ClauseMatcher object */
    unsigned liIndex;
    /** Index of the literal in the query clause */
    unsigned litIndex;
    FlatTerm* ft;
    bool opposite;
  };

  struct MatchInfo : public FlexibleTail<MatchInfo, TermList>
  {
    /** Index of the matched LitInfo in the EContext */
    unsigned liIndex;
    TermList *bindings() { return flexibleTail(); }

  private:
    void init(ILStruct* ils, unsigned liIndex, DArray<TermList>& bindingArray);

    static MatchInfo* alloc(unsigned bindCnt);

    void destroy(unsigned bindCnt);


    friend struct ILStruct;

    //these functions are deleted as we take care of the MatchInfo initialisation
    //and destruction ourselves
    MatchInfo() = delete;
    ~MatchInfo() = delete;
    void operator delete(void*) = delete;
    void* operator new(size_t,unsigned length) = delete;
  };

  /**
   * Structure with information about an indexed literal
   */
  struct alignas(8) ILStruct
  {
    ILStruct(const Literal* lit, unsigned varCnt, Stack<unsigned>& gvnStack);
    ~ILStruct();
    void putIntoSequence(ILStruct* previous_);

    bool equalsForOpMatching(const ILStruct& o) const;

    void ensureFreshness(unsigned globalTimestamp);

    USE_ALLOCATOR(ILStruct);

    struct GVArrComparator;

    unsigned depth;
    // A null continuation denotes a single SUCCESS immediately after the LIT_END.
    CodeTreeSubtree* next = nullptr;

    unsigned varCnt;

    unsigned* globalVarNumbers;

    unsigned* sortedGlobalVarNumbers;

    /** Permutation that should be applied to bindings so that they will
     *  correspond to the sortedGlobalVarNumbers */
    unsigned* globalVarPermutation;

    unsigned timestamp;
    //from here on, the values are valid only if the timestamp is current

    void addMatch(unsigned liIndex, DArray<TermList>& bindingArray);
    void deleteMatch(unsigned matchIndex);
    MatchInfo*& getMatch(unsigned matchIndex);
    const MatchInfo* getMatch(unsigned matchIndex) const;

    unsigned matchCnt;

    /** all possible lits were tried to match */
    bool visited;
    bool finished;
    bool noNonOppositeMatches;
  private:
    DArray<MatchInfo*> matches;
  };

  enum Instruction
  {
    //it means fail if data==0
    SUCCESS_OR_FAIL = 0,
    CHECK_GROUND_TERM = 1,
    LIT_END = 2,
    CHECK_FUN = 3,
    ASSIGN_VAR = 4,
    CHECK_VAR = 5,
    SEARCH_STRUCT = 6,
  };
  static const unsigned INSTRUCTION_BITS = 3;
  static_assert(SEARCH_STRUCT < 1 << INSTRUCTION_BITS, "Instruction should fit within INSTRUCTION_BITS");

  /** Structure containing a single instruction and its arguments */
  struct CodeOp
  {
    template<class T> static CodeOp getSuccess(T* ptr)
    {
      ASS(ptr); //data has to be a non-zero pointer
      CodeOp res;
      res._setData(ptr);
      res._setInstruction(SUCCESS_OR_FAIL);
      ASS(res.isSuccess());
      return res;
    }
    static CodeOp getLitEnd(ILStruct* ils);
    static CodeOp getTermOp(Instruction i, unsigned num);
    static CodeOp getGroundTermCheck(const Term* trm);
    static CodeOp getSearchStructOp(SearchStruct* ss);

    bool equalsForOpMatching(const CodeOp& o) const;

    /**
     * Return true iff CodeOp contains a success instruction
     *
     * A success instruction is either SUCCESS or SUCCESS2, because
     * on some architectures, pointers are only 4-byte aligned and
     * the instruction is stored in first three bits.
     */
    inline bool isSuccess() const { static_assert(SUCCESS_OR_FAIL==0); return _instruction()==SUCCESS_OR_FAIL && _content; }
    inline bool isFail() const { static_assert(SUCCESS_OR_FAIL==0); return !_content; }
    inline bool isLitEnd() const { return _instruction()==LIT_END; }
    inline bool isSearchStruct() const { return _instruction()==SEARCH_STRUCT; }
    inline bool isCheckFun() const { return _instruction()==CHECK_FUN; }
    inline bool isCheckGroundTerm() const { return _instruction()==CHECK_GROUND_TERM; }
    /**
     * True iff the execution never continues with the next operation in memory
     * after this one, i.e. this is the last operation of a code block.
     * (A LIT_END may be followed by its inline SUCCESS, see @b literalContinuation,
     * but that is not a continuation of the block.)
     */
    inline bool isTerminal() const { return _instruction()==SUCCESS_OR_FAIL || _instruction()==LIT_END; }

    inline Term* getTargetTerm() const
    {
      ASS(isCheckGroundTerm());
      return _data<Term>();
    }

    template<class T> inline T* getSuccessResult() const { ASS(isSuccess()); return _data<T>(); }

    inline ILStruct* getILS() { ASS(isLitEnd()); return _data<ILStruct>(); }
    inline const ILStruct* getILS() const { return _data<ILStruct>(); }

    const SearchStruct* getSearchStruct() const;
    SearchStruct* getSearchStruct();

    inline CodeOp* alternative() const { return _alternative; }
    inline void setAlternative(CodeOp* op) { ASS_NEQ(op, this); _alternative=op; }

    /**
     * The alternative as an offset relative to this operation (0 if there is none).
     * Both operations always lie in the same @b CodeTreeSubtree.
     */
    inline int32_t alternativeOffset() const
    { return _alternative ? static_cast<int32_t>(_alternative-this) : 0; }
    inline void setAlternativeOffset(int32_t ofs) { _alternative = ofs ? this+ofs : nullptr; }

    CodeOp* literalContinuation();

    /** Turn into a FAIL operation. The alternative is kept. */
    void makeFail() { static_assert(SUCCESS_OR_FAIL==0); _content = 0; }

    BITFIELD(64,
      BITFIELD_MEMBER(unsigned, _arg, _setArg, CHAR_BIT * sizeof(unsigned) - INSTRUCTION_BITS,
      BITFIELD_MEMBER(unsigned, _instruction, _setInstruction, INSTRUCTION_BITS,
      END_BITFIELD
    )))
    static_assert(sizeof(void *) <= sizeof(uint64_t), "must be able to fit a pointer into a 64-bit integer");
    template<class T>
    BITFIELD_PTR_GET(T, _data, INSTRUCTION_BITS)
    template<class T>
    BITFIELD_PTR_SET(T, _setData, INSTRUCTION_BITS)

  private:
    // bitfield
    uint64_t _content = 0;

    /**
     * Pointer to an alternative operation
     *
     * If nonzero, points either to the first operation of a code block or
     * to a SEARCH_STRUCT operation, always in the same @b CodeTreeSubtree.
     * When the subtree is moved in memory, the pointers are relocated.
     */
    CodeOp* _alternative = nullptr;
  };
  static_assert(sizeof(CodeOp)==16);

  /**
   * A contiguous array of CodeOps holding the code of one literal level of
   * the tree. It starts either at the root of the tree or at the first op that
   * is executed after a LIT_END, and contains all the code blocks reachable from
   * there up to (and including) the next LIT_ENDs. Each LIT_END refers (via its
   * ILStruct) to the subtree that is executed after it.
   *
   * Alternatives point only within a subtree, so when the whole array is
   * reallocated (when growing) the pointers are simply relocated.
   *
   * The CodeOps are stored right after the header.
   */
  struct CodeTreeSubtree
  {
    static CodeTreeSubtree* create(uint32_t capacity);
    static void deallocate(CodeTreeSubtree* st);
    /**
     * Append @b n empty operations, possibly reallocating the subtree
     * (in which case @b st is updated). Returns the index of the first one.
     */
    static uint32_t append(CodeTreeSubtree*& st, uint32_t n);

    inline CodeOp* ops() { return reinterpret_cast<CodeOp*>(this+1); }
    inline const CodeOp* ops() const { return reinterpret_cast<const CodeOp*>(this+1); }
    inline CodeOp* root() { return ops(); }
    inline CodeOp& operator[](size_t i) { ASS_L(i,_size); return ops()[i]; }
    inline uint32_t size() const { return _size; }

    static size_t bytesFor(uint32_t capacity) { return sizeof(CodeTreeSubtree)+sizeof(CodeOp)*capacity; }

    uint32_t _size;
    uint32_t _capacity;
    /** (approximate) number of unreachable operations */
    uint32_t _garbage;
    /** size right after the last compaction (or creation) */
    uint32_t _compactedSize;
  };
  static_assert(sizeof(CodeTreeSubtree)%alignof(CodeOp)==0);

  /**
   * A search structure that collects alternatives of the same
   * kind (either function symbols or ground terms) and offers
   * more efficient searching and insertion over them.
   */
  struct SearchStruct
  {
    void destroy();
    /**
     * Fills out pointer @b tgt where @b insertedOp should be
     * (or is) inserted in the structure. If @b doInsert is true
     * an entry is inserted if not found.
     */
    template<bool doInsert>
    bool getTargetOpPtr(const CodeOp& insertedOp, int32_t*& tgt);

    /**
     * Returns code op in the structure matching the content
     * of flat term entry @b ftPos. The @b ssOp is the SEARCH_STRUCT
     * operation referring to this structure.
     */
    CodeOp* getTargetOp(CodeOp* ssOp, const FlatTerm::Entry* ftPos);
    inline size_t length() const { return targets.size(); }

    enum Kind
    {
      FN_STRUCT,
      GROUND_TERM_STRUCT
    };

    Kind kind;
    /**
     * Targets as offsets relative to the SEARCH_STRUCT operation
     * (which lies in the same subtree as the targets), 0 means no target.
     */
    std::vector<int32_t> targets;

  protected:
    SearchStruct(Kind kind, size_t length);
  };

  template<SearchStruct::Kind k>
  struct SearchStructImpl
  : public SearchStruct
  {
    SearchStructImpl(size_t length);

    using T = typename std::conditional<k==SearchStruct::FN_STRUCT,unsigned,Term*>::type;

    /**
     * Tries to find the code op in @b targets at position where @b val is in @b values.
     * If exact code op is not found and @b doInsert is true, an entry is inserted
     * into @b values and @b targets. Otherwise, some code op is returned where
     * the entry should be (or is) stored as an alternative.
     */
    template<bool doInsert> int32_t& targetOp(const T& val);

    std::vector<T> values;
  };

  using FnSearchStruct = SearchStructImpl<SearchStruct::FN_STRUCT>;
  using GroundTermSearchStruct = SearchStructImpl<SearchStruct::GROUND_TERM_STRUCT>;

  typedef Stack<CodeOp> CodeStack;
  typedef DArray<TermList> BindingArray;

  /**
   * A set of query literal positions.
   * Positions outside CAPACITY are not represented.
   */
  struct LitInfoOccupation {
    static constexpr unsigned CAPACITY = 64;

    LitInfoOccupation() {}

    inline void occupy(const LitInfo& linfo)
    {
      if (linfo.litIndex < CAPACITY) {
        bitmask |= uint64_t{1} << linfo.litIndex;
      }
    }

    inline bool isOccupied(const LitInfo& linfo) const
    {
      if (linfo.litIndex < CAPACITY) {
        return bitmask & (uint64_t{1} << linfo.litIndex);
      }
      return false;
    }

    inline void occupy(LitInfoOccupation other)
    {
      bitmask |= other.bitmask;
    }

    inline bool isExclusive() const
    {
      return std::has_single_bit(bitmask);
    }

  private:
    uint64_t bitmask = 0;
  };

  // This holds the parts relevant only for Matcher<true,...>
  struct RemovingBase {
    Stack<CodeOp*>* firstsInBlocks;
    size_t initFIBDepth;
    bool matchingClauses;
    DHSet<unsigned, FnvHash, IdentityHash> range;
  };

  struct NonRemovingBase {};

  /**
   * Context for finding matches of literals
   *
   * Here the actual execution of the code of the tree takes place.
   *
   * The object is not initialized not only by constructor, but also by
   * a call to the @b init function (inheritors should implement their
   * own @b init function (possibly with other arguments) that will call
   * this one. After use, the @b deinit function should be called (if
   * present). This allows for reuse of a single object.
   */
  template<bool removing, bool checkRange>
  struct Matcher
    : public std::conditional<removing, RemovingBase, NonRemovingBase>::type
  {
    // we only want to enable checkRange if
    // removing, which works on variables
    static_assert(removing || !checkRange);

    /**
     * Backtracking point for the interpretation of the code tree.
     */
    struct BTPoint
    {
      BTPoint(size_t tp, CodeOp* op) : tp(tp), op(op) {}

      /** Position in the flat term */
      size_t tp;
      /** Pointer to the next operation */
      CodeOp* op;
    };

    struct BTPointRemoving
    {
      BTPointRemoving(size_t tp, CodeOp* op, size_t fibDepth)
      : tp(tp), op(op), fibDepth(fibDepth) {}

      size_t tp;
      CodeOp* op;
      size_t fibDepth;
    };

    inline bool finished() const { return !fresh && !_matched; }
    inline bool matched() const { return _matched && op->isLitEnd(); }
    inline bool success() const { return _matched && op->isSuccess(); }

    bool execute();

    /**
     * Pointer to the current operation
     *
     * Must be initialized by inheritor (either directly or by
     * a call to the @b prepareLiteral function).
     */
    CodeOp* op;

    BindingArray bindings;

    bool keepRecycled() const
    {
      if constexpr (removing) {
        return bindings.keepRecycled() || btStack.keepRecycled()
          || (RemovingBase::firstsInBlocks && RemovingBase::firstsInBlocks->keepRecycled());
      } else {
        return bindings.keepRecycled() || btStack.keepRecycled();
      }
    }

  protected:
    void init(const CodeTree& tree_, CodeOp* entry_, LitInfoOccupation occupied_ = LitInfoOccupation(), LitInfo* linfos_ = 0,
      size_t linfoCnt_ = 0, Stack<CodeOp*>* firstsInBlocks_ = 0);

    bool backtrack();
    bool prepareLiteral();
    bool doAssignVar();
    bool doCheckVar();
    bool doCheckFun();
    bool doCheckGroundTerm();
    bool doSearchStruct();

    /**
     * Position in the flat term
     *
     * Must be initialized by inheritor (either directly or by
     * a call to the @b prepareLiteral function).
     */
    size_t tp;
    /**
     * Flat term to be traversed
     *
     * Must be initialized by inheritor (either directly or by
     * a call to the @b prepareLiteral function).
     */
    FlatTerm* ft = nullptr;

    /** the matcher object is initialized but no execution of code was done yet */
    bool fresh;
    bool _matched;

    /** Stack containing backtracking points */
    Stack<std::conditional_t<removing,BTPointRemoving,BTPoint>> btStack;

    CodeOp* entry;
    CodeTree const* tree;

    /**
     * Array of alternative LitInfo objects
     *
     * Must be initialized by inheritor.
     */
    LitInfo* linfos;
    /**
     * Length of the @b linfos array
     *
     * Must be initialized by inheritor.
     */
    size_t linfoCnt;

    /**
     * Currently matched LitInfo object in case LitInfo objects
     * are used (they are not in TermCodeTree::TermMatcher).
     */
    size_t curLInfo;

    /**
     * First executed LitInfo object.
     * We need this value to return only once per SUCCESS CodeOp.
     */
    size_t firstLInfo;

    /**
     * Bit-Mask indicating which LitInfo objects are occupied.
     */
    LitInfoOccupation occupied;
  };

  //////// auxiliary methods //////////

  inline bool isEmpty() const { return !_root; }
  inline CodeOp* getEntryPoint() const { ASS(!isEmpty()); return _root->root(); }

  template<class Visitor>
  void visitAllOps(Visitor visitor) const;

  void printOp(std::ostream& out, const CodeOp& op, bool litStart) const;
  void printOps(std::ostream& out, const CodeTree& ct, const CodeStack& st) const;
  friend std::ostream& operator<<(std::ostream& out, const CodeTree& ct);

  //////////// insertion //////////////

  typedef DHMap<unsigned,unsigned, FnvHash, IdentityHash> VarMap;

  template<bool forLits>
  struct Compiler
  {
    Compiler(CodeStack& code);
    void updateCodeTree(CodeTree* tree);

    void nextLit();

    void handleTerm(const Term* trm);
    void handleVar(unsigned var, Stack<unsigned>* globalCounterparts = nullptr);
    void handleSubterms(const Term* trm, Stack<unsigned>& globalCounterparts);

    CodeStack& code;
    unsigned nextVarNum;
    unsigned nextGlobalVarNum;
    VarMap varMap;
    VarMap globalVarMap;
  };

  using LitCompiler = Compiler<true>;
  using TermCompiler = Compiler<false>;

  void incorporate(CodeStack& code);

  static size_t appendSegment(CodeTreeSubtree*& st, CodeStack& code, size_t start, ILStruct* prev, uint32_t& firstIdx, ILStruct*& lastILS);
  static void attachRest(CodeStack& code, size_t pos, ILStruct* ils);
  static void disposeCode(CodeStack& code, size_t cnt);

  template<SearchStruct::Kind k>
  static void compressCheckOps(CodeTreeSubtree*& st, uint32_t chainStartIdx);

  /** Re-lay out the reachable part of the subtree, dropping the garbage */
  static void compact(CodeTreeSubtree*& st);
  static void maybeCompact(CodeTreeSubtree*& st);
  void destroySubtrees(CodeTreeSubtree* st);

  //////////// removal //////////////

  void optimizeMemoryAfterRemoval(Stack<CodeOp*>* firstsInBlocks, CodeOp* removedOp);
  static void killOp(CodeOp* op);
  static bool removeDeadOp(Stack<CodeOp*>& firstsInBlocks, size_t fibStart, CodeOp* op, CodeTreeSubtree* st);

  void incTimeStamp();

  //////// member variables //////////

  bool _clauseCodeTree = false;
  unsigned _curTimeStamp = 0;

  /** maximal number of local variables in a stored term/literal (always at least 1) */
  unsigned _maxVarCnt = 1;

  CodeTreeSubtree* _root = nullptr;
};

inline CodeTree::CodeOp* CodeTree::CodeOp::literalContinuation()
{
  ASS(isLitEnd());
  auto* next = getILS()->next;
  ASS(next || (this + 1)->isSuccess());
  return next ? next->root() : this + 1;
}

}

#endif // __CodeTree__
