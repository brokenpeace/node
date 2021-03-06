// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ARM64_INSTRUCTIONS_ARM64_H_
#define V8_ARM64_INSTRUCTIONS_ARM64_H_

#include "src/arm64/constants-arm64.h"
#include "src/arm64/utils-arm64.h"
#include "src/assembler.h"
#include "src/globals.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

// ISA constants. --------------------------------------------------------------

typedef uint32_t Instr;

// The following macros initialize a float/double variable with a bit pattern
// without using static initializers: If ARM64_DEFINE_FP_STATICS is defined, the
// symbol is defined as uint32_t/uint64_t initialized with the desired bit
// pattern. Otherwise, the same symbol is declared as an external float/double.
#if defined(ARM64_DEFINE_FP_STATICS)
#define DEFINE_FLOAT16(name, value) extern const uint16_t name = value
#define DEFINE_FLOAT(name, value) extern const uint32_t name = value
#define DEFINE_DOUBLE(name, value) extern const uint64_t name = value
#else
#define DEFINE_FLOAT16(name, value) extern const float16 name
#define DEFINE_FLOAT(name, value) extern const float name
#define DEFINE_DOUBLE(name, value) extern const double name
#endif  // defined(ARM64_DEFINE_FP_STATICS)

DEFINE_FLOAT16(kFP16PositiveInfinity, 0x7c00);
DEFINE_FLOAT16(kFP16NegativeInfinity, 0xfc00);
DEFINE_FLOAT(kFP32PositiveInfinity, 0x7f800000);
DEFINE_FLOAT(kFP32NegativeInfinity, 0xff800000);
DEFINE_DOUBLE(kFP64PositiveInfinity, 0x7ff0000000000000UL);
DEFINE_DOUBLE(kFP64NegativeInfinity, 0xfff0000000000000UL);

// This value is a signalling NaN as both a double and as a float (taking the
// least-significant word).
DEFINE_DOUBLE(kFP64SignallingNaN, 0x7ff000007f800001);
DEFINE_FLOAT(kFP32SignallingNaN, 0x7f800001);

// A similar value, but as a quiet NaN.
DEFINE_DOUBLE(kFP64QuietNaN, 0x7ff800007fc00001);
DEFINE_FLOAT(kFP32QuietNaN, 0x7fc00001);

// The default NaN values (for FPCR.DN=1).
DEFINE_DOUBLE(kFP64DefaultNaN, 0x7ff8000000000000UL);
DEFINE_FLOAT(kFP32DefaultNaN, 0x7fc00000);
DEFINE_FLOAT16(kFP16DefaultNaN, 0x7e00);

#undef DEFINE_FLOAT16
#undef DEFINE_FLOAT
#undef DEFINE_DOUBLE

unsigned CalcLSDataSize(LoadStoreOp op);
unsigned CalcLSPairDataSize(LoadStorePairOp op);

enum ImmBranchType {
  UnknownBranchType = 0,
  CondBranchType    = 1,
  UncondBranchType  = 2,
  CompareBranchType = 3,
  TestBranchType    = 4
};

enum AddrMode {
  Offset,
  PreIndex,
  PostIndex
};

enum FPRounding {
  // The first four values are encodable directly by FPCR<RMode>.
  FPTieEven = 0x0,
  FPPositiveInfinity = 0x1,
  FPNegativeInfinity = 0x2,
  FPZero = 0x3,

  // The final rounding modes are only available when explicitly specified by
  // the instruction (such as with fcvta). They cannot be set in FPCR.
  FPTieAway,
  FPRoundOdd
};

enum Reg31Mode {
  Reg31IsStackPointer,
  Reg31IsZeroRegister
};

// Instructions. ---------------------------------------------------------------

class Instruction {
 public:
  V8_INLINE Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  V8_INLINE void SetInstructionBits(Instr new_instr) {
    *reinterpret_cast<Instr*>(this) = new_instr;
  }

  int Bit(int pos) const {
    return (InstructionBits() >> pos) & 1;
  }

  uint32_t Bits(int msb, int lsb) const {
    return unsigned_bitextract_32(msb, lsb, InstructionBits());
  }

  int32_t SignedBits(int msb, int lsb) const {
    int32_t bits = *(reinterpret_cast<const int32_t*>(this));
    return signed_bitextract_32(msb, lsb, bits);
  }

  Instr Mask(uint32_t mask) const {
    return InstructionBits() & mask;
  }

  V8_INLINE const Instruction* following(int count = 1) const {
    return InstructionAtOffset(count * static_cast<int>(kInstructionSize));
  }

  V8_INLINE Instruction* following(int count = 1) {
    return InstructionAtOffset(count * static_cast<int>(kInstructionSize));
  }

  V8_INLINE const Instruction* preceding(int count = 1) const {
    return following(-count);
  }

  V8_INLINE Instruction* preceding(int count = 1) {
    return following(-count);
  }

#define DEFINE_GETTER(Name, HighBit, LowBit, Func) \
  int32_t Name() const { return Func(HighBit, LowBit); }
  INSTRUCTION_FIELDS_LIST(DEFINE_GETTER)
  #undef DEFINE_GETTER

  // ImmPCRel is a compound field (not present in INSTRUCTION_FIELDS_LIST),
  // formed from ImmPCRelLo and ImmPCRelHi.
  int ImmPCRel() const {
    DCHECK(IsPCRelAddressing());
    int offset = ((ImmPCRelHi() << ImmPCRelLo_width) | ImmPCRelLo());
    int width = ImmPCRelLo_width + ImmPCRelHi_width;
    return signed_bitextract_32(width - 1, 0, offset);
  }

  uint64_t ImmLogical();
  unsigned ImmNEONabcdefgh() const;
  float ImmFP32();
  double ImmFP64();
  float ImmNEONFP32() const;
  double ImmNEONFP64() const;

  unsigned SizeLS() const {
    return CalcLSDataSize(static_cast<LoadStoreOp>(Mask(LoadStoreMask)));
  }

  unsigned SizeLSPair() const {
    return CalcLSPairDataSize(
        static_cast<LoadStorePairOp>(Mask(LoadStorePairMask)));
  }

  int NEONLSIndex(int access_size_shift) const {
    int q = NEONQ();
    int s = NEONS();
    int size = NEONLSSize();
    int index = (q << 3) | (s << 2) | size;
    return index >> access_size_shift;
  }

  // Helpers.
  bool IsCondBranchImm() const {
    return Mask(ConditionalBranchFMask) == ConditionalBranchFixed;
  }

  bool IsUncondBranchImm() const {
    return Mask(UnconditionalBranchFMask) == UnconditionalBranchFixed;
  }

  bool IsCompareBranch() const {
    return Mask(CompareBranchFMask) == CompareBranchFixed;
  }

  bool IsTestBranch() const {
    return Mask(TestBranchFMask) == TestBranchFixed;
  }

  bool IsImmBranch() const {
    return BranchType() != UnknownBranchType;
  }

  static float Imm8ToFP32(uint32_t imm8) {
    //   Imm8: abcdefgh (8 bits)
    // Single: aBbb.bbbc.defg.h000.0000.0000.0000.0000 (32 bits)
    // where B is b ^ 1
    uint32_t bits = imm8;
    uint32_t bit7 = (bits >> 7) & 0x1;
    uint32_t bit6 = (bits >> 6) & 0x1;
    uint32_t bit5_to_0 = bits & 0x3f;
    uint32_t result = (bit7 << 31) | ((32 - bit6) << 25) | (bit5_to_0 << 19);

    return bit_cast<float>(result);
  }

  static double Imm8ToFP64(uint32_t imm8) {
    //   Imm8: abcdefgh (8 bits)
    // Double: aBbb.bbbb.bbcd.efgh.0000.0000.0000.0000
    //         0000.0000.0000.0000.0000.0000.0000.0000 (64 bits)
    // where B is b ^ 1
    uint32_t bits = imm8;
    uint64_t bit7 = (bits >> 7) & 0x1;
    uint64_t bit6 = (bits >> 6) & 0x1;
    uint64_t bit5_to_0 = bits & 0x3f;
    uint64_t result = (bit7 << 63) | ((256 - bit6) << 54) | (bit5_to_0 << 48);

    return bit_cast<double>(result);
  }

  bool IsLdrLiteral() const {
    return Mask(LoadLiteralFMask) == LoadLiteralFixed;
  }

  bool IsLdrLiteralX() const {
    return Mask(LoadLiteralMask) == LDR_x_lit;
  }

  bool IsPCRelAddressing() const {
    return Mask(PCRelAddressingFMask) == PCRelAddressingFixed;
  }

  bool IsAdr() const {
    return Mask(PCRelAddressingMask) == ADR;
  }

  bool IsBrk() const { return Mask(ExceptionMask) == BRK; }

  bool IsUnresolvedInternalReference() const {
    // Unresolved internal references are encoded as two consecutive brk
    // instructions.
    return IsBrk() && following()->IsBrk();
  }

  bool IsLogicalImmediate() const {
    return Mask(LogicalImmediateFMask) == LogicalImmediateFixed;
  }

  bool IsAddSubImmediate() const {
    return Mask(AddSubImmediateFMask) == AddSubImmediateFixed;
  }

  bool IsAddSubShifted() const {
    return Mask(AddSubShiftedFMask) == AddSubShiftedFixed;
  }

  bool IsAddSubExtended() const {
    return Mask(AddSubExtendedFMask) == AddSubExtendedFixed;
  }

  // Match any loads or stores, including pairs.
  bool IsLoadOrStore() const {
    return Mask(LoadStoreAnyFMask) == LoadStoreAnyFixed;
  }

  // Match any loads, including pairs.
  bool IsLoad() const;
  // Match any stores, including pairs.
  bool IsStore() const;

  // Indicate whether Rd can be the stack pointer or the zero register. This
  // does not check that the instruction actually has an Rd field.
  Reg31Mode RdMode() const {
    // The following instructions use csp or wsp as Rd:
    //  Add/sub (immediate) when not setting the flags.
    //  Add/sub (extended) when not setting the flags.
    //  Logical (immediate) when not setting the flags.
    // Otherwise, r31 is the zero register.
    if (IsAddSubImmediate() || IsAddSubExtended()) {
      if (Mask(AddSubSetFlagsBit)) {
        return Reg31IsZeroRegister;
      } else {
        return Reg31IsStackPointer;
      }
    }
    if (IsLogicalImmediate()) {
      // Of the logical (immediate) instructions, only ANDS (and its aliases)
      // can set the flags. The others can all write into csp.
      // Note that some logical operations are not available to
      // immediate-operand instructions, so we have to combine two masks here.
      if (Mask(LogicalImmediateMask & LogicalOpMask) == ANDS) {
        return Reg31IsZeroRegister;
      } else {
        return Reg31IsStackPointer;
      }
    }
    return Reg31IsZeroRegister;
  }

  // Indicate whether Rn can be the stack pointer or the zero register. This
  // does not check that the instruction actually has an Rn field.
  Reg31Mode RnMode() const {
    // The following instructions use csp or wsp as Rn:
    //  All loads and stores.
    //  Add/sub (immediate).
    //  Add/sub (extended).
    // Otherwise, r31 is the zero register.
    if (IsLoadOrStore() || IsAddSubImmediate() || IsAddSubExtended()) {
      return Reg31IsStackPointer;
    }
    return Reg31IsZeroRegister;
  }

  ImmBranchType BranchType() const {
    if (IsCondBranchImm()) {
      return CondBranchType;
    } else if (IsUncondBranchImm()) {
      return UncondBranchType;
    } else if (IsCompareBranch()) {
      return CompareBranchType;
    } else if (IsTestBranch()) {
      return TestBranchType;
    } else {
      return UnknownBranchType;
    }
  }

  static int ImmBranchRangeBitwidth(ImmBranchType branch_type) {
    switch (branch_type) {
      case UncondBranchType:
        return ImmUncondBranch_width;
      case CondBranchType:
        return ImmCondBranch_width;
      case CompareBranchType:
        return ImmCmpBranch_width;
      case TestBranchType:
        return ImmTestBranch_width;
      default:
        UNREACHABLE();
    }
  }

  // The range of the branch instruction, expressed as 'instr +- range'.
  static int32_t ImmBranchRange(ImmBranchType branch_type) {
    return
      (1 << (ImmBranchRangeBitwidth(branch_type) + kInstructionSizeLog2)) / 2 -
      kInstructionSize;
  }

  int ImmBranch() const {
    switch (BranchType()) {
      case CondBranchType: return ImmCondBranch();
      case UncondBranchType: return ImmUncondBranch();
      case CompareBranchType: return ImmCmpBranch();
      case TestBranchType: return ImmTestBranch();
      default: UNREACHABLE();
    }
    return 0;
  }

  int ImmUnresolvedInternalReference() const {
    DCHECK(IsUnresolvedInternalReference());
    // Unresolved references are encoded as two consecutive brk instructions.
    // The associated immediate is made of the two 16-bit payloads.
    int32_t high16 = ImmException();
    int32_t low16 = following()->ImmException();
    return (high16 << 16) | low16;
  }

  bool IsBranchAndLinkToRegister() const {
    return Mask(UnconditionalBranchToRegisterMask) == BLR;
  }

  bool IsMovz() const {
    return (Mask(MoveWideImmediateMask) == MOVZ_x) ||
           (Mask(MoveWideImmediateMask) == MOVZ_w);
  }

  bool IsMovk() const {
    return (Mask(MoveWideImmediateMask) == MOVK_x) ||
           (Mask(MoveWideImmediateMask) == MOVK_w);
  }

  bool IsMovn() const {
    return (Mask(MoveWideImmediateMask) == MOVN_x) ||
           (Mask(MoveWideImmediateMask) == MOVN_w);
  }

  bool IsNop(int n) {
    // A marking nop is an instruction
    //   mov r<n>,  r<n>
    // which is encoded as
    //   orr r<n>, xzr, r<n>
    return (Mask(LogicalShiftedMask) == ORR_x) &&
           (Rd() == Rm()) &&
           (Rd() == n);
  }

  // Find the PC offset encoded in this instruction. 'this' may be a branch or
  // a PC-relative addressing instruction.
  // The offset returned is unscaled.
  int64_t ImmPCOffset();

  // Find the target of this instruction. 'this' may be a branch or a
  // PC-relative addressing instruction.
  Instruction* ImmPCOffsetTarget();

  static bool IsValidImmPCOffset(ImmBranchType branch_type, ptrdiff_t offset);
  bool IsTargetInImmPCOffsetRange(Instruction* target);
  // Patch a PC-relative offset to refer to 'target'. 'this' may be a branch or
  // a PC-relative addressing instruction.
  void SetImmPCOffsetTarget(AssemblerBase::IsolateData isolate_data,
                            Instruction* target);
  void SetUnresolvedInternalReferenceImmTarget(AssemblerBase::IsolateData,
                                               Instruction* target);
  // Patch a literal load instruction to load from 'source'.
  void SetImmLLiteral(Instruction* source);

  uintptr_t LiteralAddress() {
    int offset = ImmLLiteral() << kLoadLiteralScaleLog2;
    return reinterpret_cast<uintptr_t>(this) + offset;
  }

  enum CheckAlignment { NO_CHECK, CHECK_ALIGNMENT };

  V8_INLINE const Instruction* InstructionAtOffset(
      int64_t offset, CheckAlignment check = CHECK_ALIGNMENT) const {
    // The FUZZ_disasm test relies on no check being done.
    DCHECK(check == NO_CHECK || IsAligned(offset, kInstructionSize));
    return this + offset;
  }

  V8_INLINE Instruction* InstructionAtOffset(
      int64_t offset, CheckAlignment check = CHECK_ALIGNMENT) {
    // The FUZZ_disasm test relies on no check being done.
    DCHECK(check == NO_CHECK || IsAligned(offset, kInstructionSize));
    return this + offset;
  }

  template<typename T> V8_INLINE static Instruction* Cast(T src) {
    return reinterpret_cast<Instruction*>(src);
  }

  V8_INLINE ptrdiff_t DistanceTo(Instruction* target) {
    return reinterpret_cast<Address>(target) - reinterpret_cast<Address>(this);
  }


  static const int ImmPCRelRangeBitwidth = 21;
  static bool IsValidPCRelOffset(ptrdiff_t offset) { return is_int21(offset); }
  void SetPCRelImmTarget(AssemblerBase::IsolateData isolate_data,
                         Instruction* target);
  void SetBranchImmTarget(Instruction* target);
};

// Functions for handling NEON vector format information.
enum VectorFormat {
  kFormatUndefined = 0xffffffff,
  kFormat8B = NEON_8B,
  kFormat16B = NEON_16B,
  kFormat4H = NEON_4H,
  kFormat8H = NEON_8H,
  kFormat2S = NEON_2S,
  kFormat4S = NEON_4S,
  kFormat1D = NEON_1D,
  kFormat2D = NEON_2D,

  // Scalar formats. We add the scalar bit to distinguish between scalar and
  // vector enumerations; the bit is always set in the encoding of scalar ops
  // and always clear for vector ops. Although kFormatD and kFormat1D appear
  // to be the same, their meaning is subtly different. The first is a scalar
  // operation, the second a vector operation that only affects one lane.
  kFormatB = NEON_B | NEONScalar,
  kFormatH = NEON_H | NEONScalar,
  kFormatS = NEON_S | NEONScalar,
  kFormatD = NEON_D | NEONScalar
};

VectorFormat VectorFormatHalfWidth(VectorFormat vform);
VectorFormat VectorFormatDoubleWidth(VectorFormat vform);
VectorFormat VectorFormatDoubleLanes(VectorFormat vform);
VectorFormat VectorFormatHalfLanes(VectorFormat vform);
VectorFormat ScalarFormatFromLaneSize(int lanesize);
VectorFormat VectorFormatHalfWidthDoubleLanes(VectorFormat vform);
VectorFormat VectorFormatFillQ(VectorFormat vform);
VectorFormat ScalarFormatFromFormat(VectorFormat vform);
unsigned RegisterSizeInBitsFromFormat(VectorFormat vform);
unsigned RegisterSizeInBytesFromFormat(VectorFormat vform);
int LaneSizeInBytesFromFormat(VectorFormat vform);
unsigned LaneSizeInBitsFromFormat(VectorFormat vform);
int LaneSizeInBytesLog2FromFormat(VectorFormat vform);
int LaneCountFromFormat(VectorFormat vform);
int MaxLaneCountFromFormat(VectorFormat vform);
bool IsVectorFormat(VectorFormat vform);
int64_t MaxIntFromFormat(VectorFormat vform);
int64_t MinIntFromFormat(VectorFormat vform);
uint64_t MaxUintFromFormat(VectorFormat vform);

// Where Instruction looks at instructions generated by the Assembler,
// InstructionSequence looks at instructions sequences generated by the
// MacroAssembler.
class InstructionSequence : public Instruction {
 public:
  static InstructionSequence* At(Address address) {
    return reinterpret_cast<InstructionSequence*>(address);
  }

  // Sequences generated by MacroAssembler::InlineData().
  bool IsInlineData() const;
  uint64_t InlineData() const;
};


// Simulator/Debugger debug instructions ---------------------------------------
// Each debug marker is represented by a HLT instruction. The immediate comment
// field in the instruction is used to identify the type of debug marker. Each
// marker encodes arguments in a different way, as described below.

// Indicate to the Debugger that the instruction is a redirected call.
const Instr kImmExceptionIsRedirectedCall = 0xca11;

// Represent unreachable code. This is used as a guard in parts of the code that
// should not be reachable, such as in data encoded inline in the instructions.
const Instr kImmExceptionIsUnreachable = 0xdebf;

// A pseudo 'printf' instruction. The arguments will be passed to the platform
// printf method.
const Instr kImmExceptionIsPrintf = 0xdeb1;
// Most parameters are stored in ARM64 registers as if the printf
// pseudo-instruction was a call to the real printf method:
//      x0: The format string.
//   x1-x7: Optional arguments.
//   d0-d7: Optional arguments.
//
// Also, the argument layout is described inline in the instructions:
//  - arg_count: The number of arguments.
//  - arg_pattern: A set of PrintfArgPattern values, packed into two-bit fields.
//
// Floating-point and integer arguments are passed in separate sets of registers
// in AAPCS64 (even for varargs functions), so it is not possible to determine
// the type of each argument without some information about the values that were
// passed in. This information could be retrieved from the printf format string,
// but the format string is not trivial to parse so we encode the relevant
// information with the HLT instruction.
const unsigned kPrintfArgCountOffset = 1 * kInstructionSize;
const unsigned kPrintfArgPatternListOffset = 2 * kInstructionSize;
const unsigned kPrintfLength = 3 * kInstructionSize;

const unsigned kPrintfMaxArgCount = 4;

// The argument pattern is a set of two-bit-fields, each with one of the
// following values:
enum PrintfArgPattern {
  kPrintfArgW = 1,
  kPrintfArgX = 2,
  // There is no kPrintfArgS because floats are always converted to doubles in C
  // varargs calls.
  kPrintfArgD = 3
};
static const unsigned kPrintfArgPatternBits = 2;

// A pseudo 'debug' instruction.
const Instr kImmExceptionIsDebug = 0xdeb0;
// Parameters are inlined in the code after a debug pseudo-instruction:
// - Debug code.
// - Debug parameters.
// - Debug message string. This is a NULL-terminated ASCII string, padded to
//   kInstructionSize so that subsequent instructions are correctly aligned.
// - A kImmExceptionIsUnreachable marker, to catch accidental execution of the
//   string data.
const unsigned kDebugCodeOffset = 1 * kInstructionSize;
const unsigned kDebugParamsOffset = 2 * kInstructionSize;
const unsigned kDebugMessageOffset = 3 * kInstructionSize;

// Debug parameters.
// Used without a TRACE_ option, the Debugger will print the arguments only
// once. Otherwise TRACE_ENABLE and TRACE_DISABLE will enable or disable tracing
// before every instruction for the specified LOG_ parameters.
//
// TRACE_OVERRIDE enables the specified LOG_ parameters, and disabled any
// others that were not specified.
//
// For example:
//
// __ debug("print registers and fp registers", 0, LOG_REGS | LOG_VREGS);
// will print the registers and fp registers only once.
//
// __ debug("trace disasm", 1, TRACE_ENABLE | LOG_DISASM);
// starts disassembling the code.
//
// __ debug("trace rets", 2, TRACE_ENABLE | LOG_REGS);
// adds the general purpose registers to the trace.
//
// __ debug("stop regs", 3, TRACE_DISABLE | LOG_REGS);
// stops tracing the registers.
const unsigned kDebuggerTracingDirectivesMask = 3 << 6;
enum DebugParameters {
  NO_PARAM = 0,
  BREAK = 1 << 0,
  LOG_DISASM = 1 << 1,    // Use only with TRACE. Disassemble the code.
  LOG_REGS = 1 << 2,      // Log general purpose registers.
  LOG_VREGS = 1 << 3,     // Log NEON and floating-point registers.
  LOG_SYS_REGS = 1 << 4,  // Log the status flags.
  LOG_WRITE = 1 << 5,     // Log any memory write.

  LOG_NONE = 0,
  LOG_STATE = LOG_REGS | LOG_VREGS | LOG_SYS_REGS,
  LOG_ALL = LOG_DISASM | LOG_STATE | LOG_WRITE,

  // Trace control.
  TRACE_ENABLE = 1 << 6,
  TRACE_DISABLE = 2 << 6,
  TRACE_OVERRIDE = 3 << 6
};

enum NEONFormat {
  NF_UNDEF = 0,
  NF_8B = 1,
  NF_16B = 2,
  NF_4H = 3,
  NF_8H = 4,
  NF_2S = 5,
  NF_4S = 6,
  NF_1D = 7,
  NF_2D = 8,
  NF_B = 9,
  NF_H = 10,
  NF_S = 11,
  NF_D = 12
};

static const unsigned kNEONFormatMaxBits = 6;

struct NEONFormatMap {
  // The bit positions in the instruction to consider.
  uint8_t bits[kNEONFormatMaxBits];

  // Mapping from concatenated bits to format.
  NEONFormat map[1 << kNEONFormatMaxBits];
};

class NEONFormatDecoder {
 public:
  enum SubstitutionMode { kPlaceholder, kFormat };

  // Construct a format decoder with increasingly specific format maps for each
  // substitution. If no format map is specified, the default is the integer
  // format map.
  explicit NEONFormatDecoder(const Instruction* instr);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format0,
                    const NEONFormatMap* format1);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format0,
                    const NEONFormatMap* format1, const NEONFormatMap* format2);

  // Set the format mapping for all or individual substitutions.
  void SetFormatMaps(const NEONFormatMap* format0,
                     const NEONFormatMap* format1 = NULL,
                     const NEONFormatMap* format2 = NULL);
  void SetFormatMap(unsigned index, const NEONFormatMap* format);

  // Substitute %s in the input string with the placeholder string for each
  // register, ie. "'B", "'H", etc.
  const char* SubstitutePlaceholders(const char* string);

  // Substitute %s in the input string with a new string based on the
  // substitution mode.
  const char* Substitute(const char* string, SubstitutionMode mode0 = kFormat,
                         SubstitutionMode mode1 = kFormat,
                         SubstitutionMode mode2 = kFormat);

  // Append a "2" to a mnemonic string based of the state of the Q bit.
  const char* Mnemonic(const char* mnemonic);

  VectorFormat GetVectorFormat(int format_index = 0);
  VectorFormat GetVectorFormat(const NEONFormatMap* format_map);

  // Built in mappings for common cases.

  // The integer format map uses three bits (Q, size<1:0>) to encode the
  // "standard" set of NEON integer vector formats.
  static const NEONFormatMap* IntegerFormatMap() {
    static const NEONFormatMap map = {
        {23, 22, 30},
        {NF_8B, NF_16B, NF_4H, NF_8H, NF_2S, NF_4S, NF_UNDEF, NF_2D}};
    return &map;
  }

  // The long integer format map uses two bits (size<1:0>) to encode the
  // long set of NEON integer vector formats. These are used in narrow, wide
  // and long operations.
  static const NEONFormatMap* LongIntegerFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_8H, NF_4S, NF_2D}};
    return &map;
  }

  // The FP format map uses two bits (Q, size<0>) to encode the NEON FP vector
  // formats: NF_2S, NF_4S, NF_2D.
  static const NEONFormatMap* FPFormatMap() {
    // The FP format map assumes two bits (Q, size<0>) are used to encode the
    // NEON FP vector formats: NF_2S, NF_4S, NF_2D.
    static const NEONFormatMap map = {{22, 30},
                                      {NF_2S, NF_4S, NF_UNDEF, NF_2D}};
    return &map;
  }

  // The load/store format map uses three bits (Q, 11, 10) to encode the
  // set of NEON vector formats.
  static const NEONFormatMap* LoadStoreFormatMap() {
    static const NEONFormatMap map = {
        {11, 10, 30},
        {NF_8B, NF_16B, NF_4H, NF_8H, NF_2S, NF_4S, NF_1D, NF_2D}};
    return &map;
  }

  // The logical format map uses one bit (Q) to encode the NEON vector format:
  // NF_8B, NF_16B.
  static const NEONFormatMap* LogicalFormatMap() {
    static const NEONFormatMap map = {{30}, {NF_8B, NF_16B}};
    return &map;
  }

  // The triangular format map uses between two and five bits to encode the NEON
  // vector format:
  // xxx10->8B, xxx11->16B, xx100->4H, xx101->8H
  // x1000->2S, x1001->4S,  10001->2D, all others undefined.
  static const NEONFormatMap* TriangularFormatMap() {
    static const NEONFormatMap map = {
        {19, 18, 17, 16, 30},
        {NF_UNDEF, NF_UNDEF, NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_2S,    NF_4S,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_UNDEF, NF_2D,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_2S,    NF_4S,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B}};
    return &map;
  }

  // The scalar format map uses two bits (size<1:0>) to encode the NEON scalar
  // formats: NF_B, NF_H, NF_S, NF_D.
  static const NEONFormatMap* ScalarFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_B, NF_H, NF_S, NF_D}};
    return &map;
  }

  // The long scalar format map uses two bits (size<1:0>) to encode the longer
  // NEON scalar formats: NF_H, NF_S, NF_D.
  static const NEONFormatMap* LongScalarFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_H, NF_S, NF_D}};
    return &map;
  }

  // The FP scalar format map assumes one bit (size<0>) is used to encode the
  // NEON FP scalar formats: NF_S, NF_D.
  static const NEONFormatMap* FPScalarFormatMap() {
    static const NEONFormatMap map = {{22}, {NF_S, NF_D}};
    return &map;
  }

  // The triangular scalar format map uses between one and four bits to encode
  // the NEON FP scalar formats:
  // xxx1->B, xx10->H, x100->S, 1000->D, all others undefined.
  static const NEONFormatMap* TriangularScalarFormatMap() {
    static const NEONFormatMap map = {
        {19, 18, 17, 16},
        {NF_UNDEF, NF_B, NF_H, NF_B, NF_S, NF_B, NF_H, NF_B, NF_D, NF_B, NF_H,
         NF_B, NF_S, NF_B, NF_H, NF_B}};
    return &map;
  }

 private:
  // Get a pointer to a string that represents the format or placeholder for
  // the specified substitution index, based on the format map and instruction.
  const char* GetSubstitute(int index, SubstitutionMode mode);

  // Get the NEONFormat enumerated value for bits obtained from the
  // instruction based on the specified format mapping.
  NEONFormat GetNEONFormat(const NEONFormatMap* format_map);

  // Convert a NEONFormat into a string.
  static const char* NEONFormatAsString(NEONFormat format);

  // Convert a NEONFormat into a register placeholder string.
  static const char* NEONFormatAsPlaceholder(NEONFormat format);

  // Select bits from instrbits_ defined by the bits array, concatenate them,
  // and return the value.
  uint8_t PickBits(const uint8_t bits[]);

  Instr instrbits_;
  const NEONFormatMap* formats_[3];
  char form_buffer_[64];
  char mne_buffer_[16];
};
}  // namespace internal
}  // namespace v8


#endif  // V8_ARM64_INSTRUCTIONS_ARM64_H_
