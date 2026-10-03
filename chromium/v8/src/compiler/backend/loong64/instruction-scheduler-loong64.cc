// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/instruction-scheduler.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }

int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
    case kLa64AbsD:
    case kLa64AbsS:
    case kLa64Add:
    case kLa64AddD:
    case kLa64AddS:
    case kLa64And:
    case kLa64And32:
    case kLa64AssertEqual:
    case kLa64BitcastDL:
    case kLa64BitcastLD:
    case kLa64ByteSwap32:
    case kLa64ByteSwap64:
    case kLa64CeilWD:
    case kLa64CeilWS:
    case kLa64Clz:
    case kLa64Cmp:
    case kLa64CmpD:
    case kLa64CmpS:
    case kLa64Ctz:
    case kLa64CvtDL:
    case kLa64CvtDS:
    case kLa64CvtDUl:
    case kLa64CvtDUw:
    case kLa64CvtDW:
    case kLa64CvtSD:
    case kLa64CvtSL:
    case kLa64CvtSUl:
    case kLa64CvtSUw:
    case kLa64CvtSW:
    case kLa64DMulHigh:
    case kLa64MulHighU:
    case kLa64Dadd:
    case kLa64DaddOvf:
    case kLa64Dclz:
    case kLa64Dctz:
    case kLa64Ddiv:
    case kLa64DdivU:
    case kLa64Dext:
    case kLa64Dins:
    case kLa64Div:
    case kLa64DivD:
    case kLa64DivS:
    case kLa64DivU:
    case kLa64Dlsa:
    case kLa64Dmod:
    case kLa64DmodU:
    case kLa64Dmul:
    case kLa64Dpopcnt:
    case kLa64Dror:
    case kLa64Dsar:
    case kLa64Dshl:
    case kLa64Dshr:
    case kLa64Dsub:
    case kLa64DsubOvf:
    case kLa64Ext:
    case kLa64F64x2Abs:
    case kLa64F64x2Neg:
    case kLa64F64x2Sqrt:
    case kLa64F64x2Add:
    case kLa64F64x2Sub:
    case kLa64F64x2Mul:
    case kLa64F64x2Div:
    case kLa64F64x2Min:
    case kLa64F64x2Max:
    case kLa64F64x2Eq:
    case kLa64F64x2Ne:
    case kLa64F64x2Lt:
    case kLa64F64x2Le:
    case kLa64F64x2Pmin:
    case kLa64F64x2Pmax:
    case kLa64F64x2Ceil:
    case kLa64F64x2Floor:
    case kLa64F64x2Trunc:
    case kLa64F64x2NearestInt:
    case kLa64I64x2Splat:
    case kLa64I64x2ExtractLane:
    case kLa64I64x2ReplaceLane:
    case kLa64I64x2Add:
    case kLa64I64x2Sub:
    case kLa64I64x2Mul:
    case kLa64I64x2Neg:
    case kLa64I64x2Shl:
    case kLa64I64x2ShrS:
    case kLa64I64x2ShrU:
    case kLa64F32x4Abs:
    case kLa64F32x4Add:
    case kLa64F32x4AddHoriz:
    case kLa64F32x4Eq:
    case kLa64F32x4ExtractLane:
    case kLa64F32x4Lt:
    case kLa64F32x4Le:
    case kLa64F32x4Max:
    case kLa64F32x4Min:
    case kLa64F32x4Mul:
    case kLa64F32x4Div:
    case kLa64F32x4Ne:
    case kLa64F32x4Neg:
    case kLa64F32x4Sqrt:
    case kLa64F32x4RecipApprox:
    case kLa64F32x4RecipSqrtApprox:
    case kLa64F32x4ReplaceLane:
    case kLa64F32x4SConvertI32x4:
    case kLa64F32x4Splat:
    case kLa64F32x4Sub:
    case kLa64F32x4UConvertI32x4:
    case kLa64F32x4Pmin:
    case kLa64F32x4Pmax:
    case kLa64F32x4Ceil:
    case kLa64F32x4Floor:
    case kLa64F32x4Trunc:
    case kLa64F32x4NearestInt:
    case kLa64F64x2Splat:
    case kLa64F64x2ExtractLane:
    case kLa64F64x2ReplaceLane:
    case kLa64Float32Max:
    case kLa64Float32Min:
    case kLa64Float32RoundDown:
    case kLa64Float32RoundTiesEven:
    case kLa64Float32RoundTruncate:
    case kLa64Float32RoundUp:
    case kLa64Float64ExtractLowWord32:
    case kLa64Float64ExtractHighWord32:
    case kLa64Float64InsertLowWord32:
    case kLa64Float64InsertHighWord32:
    case kLa64Float64Max:
    case kLa64Float64Min:
    case kLa64Float64RoundDown:
    case kLa64Float64RoundTiesEven:
    case kLa64Float64RoundTruncate:
    case kLa64Float64RoundUp:
    case kLa64Float64SilenceNaN:
    case kLa64FloorWD:
    case kLa64FloorWS:
    case kLa64I16x8Add:
    case kLa64I16x8AddHoriz:
    case kLa64I16x8AddSaturateS:
    case kLa64I16x8AddSaturateU:
    case kLa64I16x8Eq:
    case kLa64I16x8ExtractLaneU:
    case kLa64I16x8ExtractLaneS:
    case kLa64I16x8GeS:
    case kLa64I16x8GeU:
    case kLa64I16x8GtS:
    case kLa64I16x8GtU:
    case kLa64I16x8MaxS:
    case kLa64I16x8MaxU:
    case kLa64I16x8MinS:
    case kLa64I16x8MinU:
    case kLa64I16x8Mul:
    case kLa64I16x8Ne:
    case kLa64I16x8Neg:
    case kLa64I16x8ReplaceLane:
    case kLa64I8x16SConvertI16x8:
    case kLa64I16x8SConvertI32x4:
    case kLa64I16x8SConvertI8x16High:
    case kLa64I16x8SConvertI8x16Low:
    case kLa64I16x8Shl:
    case kLa64I16x8ShrS:
    case kLa64I16x8ShrU:
    case kLa64I16x8Splat:
    case kLa64I16x8Sub:
    case kLa64I16x8SubSaturateS:
    case kLa64I16x8SubSaturateU:
    case kLa64I8x16UConvertI16x8:
    case kLa64I16x8UConvertI32x4:
    case kLa64I16x8UConvertI8x16High:
    case kLa64I16x8UConvertI8x16Low:
    case kLa64I16x8RoundingAverageU:
    case kLa64I16x8Abs:
    case kLa64I16x8BitMask:
    case kLa64I32x4Add:
    case kLa64I32x4AddHoriz:
    case kLa64I32x4Eq:
    case kLa64I32x4ExtractLane:
    case kLa64I32x4GeS:
    case kLa64I32x4GeU:
    case kLa64I32x4GtS:
    case kLa64I32x4GtU:
    case kLa64I32x4MaxS:
    case kLa64I32x4MaxU:
    case kLa64I32x4MinS:
    case kLa64I32x4MinU:
    case kLa64I32x4Mul:
    case kLa64I32x4Ne:
    case kLa64I32x4Neg:
    case kLa64I32x4ReplaceLane:
    case kLa64I32x4SConvertF32x4:
    case kLa64I32x4SConvertI16x8High:
    case kLa64I32x4SConvertI16x8Low:
    case kLa64I32x4Shl:
    case kLa64I32x4ShrS:
    case kLa64I32x4ShrU:
    case kLa64I32x4Splat:
    case kLa64I32x4Sub:
    case kLa64I32x4UConvertF32x4:
    case kLa64I32x4UConvertI16x8High:
    case kLa64I32x4UConvertI16x8Low:
    case kLa64I32x4Abs:
    case kLa64I32x4BitMask:
    case kLa64I8x16Add:
    case kLa64I8x16AddSaturateS:
    case kLa64I8x16AddSaturateU:
    case kLa64I8x16Eq:
    case kLa64I8x16ExtractLaneU:
    case kLa64I8x16ExtractLaneS:
    case kLa64I8x16GeS:
    case kLa64I8x16GeU:
    case kLa64I8x16GtS:
    case kLa64I8x16GtU:
    case kLa64I8x16MaxS:
    case kLa64I8x16MaxU:
    case kLa64I8x16MinS:
    case kLa64I8x16MinU:
    case kLa64I8x16Mul:
    case kLa64I8x16Ne:
    case kLa64I8x16Neg:
    case kLa64I8x16ReplaceLane:
    case kLa64I8x16Shl:
    case kLa64I8x16ShrS:
    case kLa64I8x16ShrU:
    case kLa64I8x16Splat:
    case kLa64I8x16Sub:
    case kLa64I8x16SubSaturateS:
    case kLa64I8x16SubSaturateU:
    case kLa64I8x16RoundingAverageU:
    case kLa64I8x16Abs:
    case kLa64I8x16BitMask:
    case kLa64Ins:
    case kLa64Lsa:
    case kLa64MaxD:
    case kLa64MaxS:
    case kLa64MinD:
    case kLa64MinS:
    case kLa64Mod:
    case kLa64ModU:
    case kLa64Mov:
    case kLa64Mul:
    case kLa64MulD:
    case kLa64MulHigh:
    case kLa64MulOvf:
    case kLa64MulS:
    case kLa64NegD:
    case kLa64NegS:
    case kLa64Nor:
    case kLa64Nor32:
    case kLa64Or:
    case kLa64Or32:
    case kLa64Popcnt:
    case kLa64Ror:
    case kLa64RoundWD:
    case kLa64RoundWS:
    case kLa64S128And:
    case kLa64S128Or:
    case kLa64S128Not:
    case kLa64S128Select:
    case kLa64S128AndNot:
    case kLa64S128Xor:
    case kLa64S128Const:
    case kLa64S128Zero:
    case kLa64S128AllOnes:
    case kLa64S16x8InterleaveEven:
    case kLa64S16x8InterleaveOdd:
    case kLa64S16x8InterleaveLeft:
    case kLa64S16x8InterleaveRight:
    case kLa64S16x8PackEven:
    case kLa64S16x8PackOdd:
    case kLa64S16x2Reverse:
    case kLa64S16x4Reverse:
    case kLa64V8x16AllTrue:
    case kLa64V8x16AnyTrue:
    case kLa64V32x4AllTrue:
    case kLa64V32x4AnyTrue:
    case kLa64V16x8AllTrue:
    case kLa64V16x8AnyTrue:
    case kLa64S32x4InterleaveEven:
    case kLa64S32x4InterleaveOdd:
    case kLa64S32x4InterleaveLeft:
    case kLa64S32x4InterleaveRight:
    case kLa64S32x4PackEven:
    case kLa64S32x4PackOdd:
    case kLa64S32x4Shuffle:
    case kLa64S8x16Concat:
    case kLa64S8x16InterleaveEven:
    case kLa64S8x16InterleaveOdd:
    case kLa64S8x16InterleaveLeft:
    case kLa64S8x16InterleaveRight:
    case kLa64S8x16PackEven:
    case kLa64S8x16PackOdd:
    case kLa64S8x2Reverse:
    case kLa64S8x4Reverse:
    case kLa64S8x8Reverse:
    case kLa64I8x16Shuffle:
    case kLa64I8x16Swizzle:
    case kLa64Sar:
    case kLa64Seb:
    case kLa64Seh:
    case kLa64Shl:
    case kLa64Shr:
    case kLa64SqrtD:
    case kLa64SqrtS:
    case kLa64Sub:
    case kLa64SubD:
    case kLa64SubS:
    case kLa64TruncLD:
    case kLa64TruncLS:
    case kLa64TruncUlD:
    case kLa64TruncUlS:
    case kLa64TruncUwD:
    case kLa64TruncUwS:
    case kLa64TruncWD:
    case kLa64TruncWS:
    case kLa64Tst:
    case kLa64Xor:
    case kLa64Xor32:
      return kNoOpcodeFlags;

    case kLa64Lb:
    case kLa64Lbu:
    case kLa64Ld:
    case kLa64Ldc1:
    case kLa64Lh:
    case kLa64Lhu:
    case kLa64Lw:
    case kLa64Lwc1:
    case kLa64Lwu:
    case kLa64Peek:
    case kLa64Uld:
    case kLa64Uldc1:
    case kLa64Ulh:
    case kLa64Ulhu:
    case kLa64Ulw:
    case kLa64Ulwu:
    case kLa64Ulwc1:
    case kLa64S8x16LoadSplat:
    case kLa64S16x8LoadSplat:
    case kLa64S32x4LoadSplat:
    case kLa64S64x2LoadSplat:
    case kLa64I16x8Load8x8S:
    case kLa64I16x8Load8x8U:
    case kLa64I32x4Load16x4S:
    case kLa64I32x4Load16x4U:
    case kLa64I64x2Load32x2S:
    case kLa64I64x2Load32x2U:
    case kLa64Word64AtomicLoadUint8:
    case kLa64Word64AtomicLoadUint16:
    case kLa64Word64AtomicLoadUint32:
    case kLa64Word64AtomicLoadUint64:

      return kIsLoadOperation;

    case kLa64ModD:
    case kLa64ModS:
    case kLa64Push:
    case kLa64Sb:
    case kLa64Sd:
    case kLa64Sdc1:
    case kLa64Sh:
    case kLa64StackClaim:
    case kLa64StoreToStackSlot:
    case kLa64Sw:
    case kLa64Swc1:
    case kLa64Usd:
    case kLa64Usdc1:
    case kLa64Ush:
    case kLa64Usw:
    case kLa64Uswc1:
    case kLa64Sync:
    case kLa64Word64AtomicStoreWord8:
    case kLa64Word64AtomicStoreWord16:
    case kLa64Word64AtomicStoreWord32:
    case kLa64Word64AtomicStoreWord64:
    case kLa64Word64AtomicAddUint8:
    case kLa64Word64AtomicAddUint16:
    case kLa64Word64AtomicAddUint32:
    case kLa64Word64AtomicAddUint64:
    case kLa64Word64AtomicSubUint8:
    case kLa64Word64AtomicSubUint16:
    case kLa64Word64AtomicSubUint32:
    case kLa64Word64AtomicSubUint64:
    case kLa64Word64AtomicAndUint8:
    case kLa64Word64AtomicAndUint16:
    case kLa64Word64AtomicAndUint32:
    case kLa64Word64AtomicAndUint64:
    case kLa64Word64AtomicOrUint8:
    case kLa64Word64AtomicOrUint16:
    case kLa64Word64AtomicOrUint32:
    case kLa64Word64AtomicOrUint64:
    case kLa64Word64AtomicXorUint8:
    case kLa64Word64AtomicXorUint16:
    case kLa64Word64AtomicXorUint32:
    case kLa64Word64AtomicXorUint64:
    case kLa64Word64AtomicExchangeUint8:
    case kLa64Word64AtomicExchangeUint16:
    case kLa64Word64AtomicExchangeUint32:
    case kLa64Word64AtomicExchangeUint64:
    case kLa64Word64AtomicCompareExchangeUint8:
    case kLa64Word64AtomicCompareExchangeUint16:
    case kLa64Word64AtomicCompareExchangeUint32:
    case kLa64Word64AtomicCompareExchangeUint64:
      return kHasSideEffect;

#define CASE(Name) case k##Name:
      COMMON_ARCH_OPCODE_LIST(CASE)
#undef CASE
      // Already covered in architecture independent code.
      UNREACHABLE();
  }

  UNREACHABLE();
}

enum Latency {
  BRANCH = 4,  // Estimated max.
  RINT_S = 4,  // Estimated.
  RINT_D = 4,  // Estimated.

  MULT = 4,
  MULTU = 4,
  DMULT = 4,
  DMULTU = 4,

  MUL = 7,
  DMUL = 7,
  MUH = 7,
  MUHU = 7,
  DMUH = 7,
  DMUHU = 7,

  DIV = 50,  // Min:11 Max:50
  DDIV = 50,
  DIVU = 50,
  DDIVU = 50,

  ABS_S = 4,
  ABS_D = 4,
  NEG_S = 4,
  NEG_D = 4,
  ADD_S = 4,
  ADD_D = 4,
  SUB_S = 4,
  SUB_D = 4,
  MAX_S = 4,  // Estimated.
  MIN_S = 4,
  MAX_D = 4,  // Estimated.
  MIN_D = 4,
  C_cond_S = 4,
  C_cond_D = 4,
  MUL_S = 4,

  MADD_S = 4,
  MSUB_S = 4,
  NMADD_S = 4,
  NMSUB_S = 4,

  CABS_cond_S = 4,
  CABS_cond_D = 4,

  CVT_D_S = 4,
  CVT_PS_PW = 4,

  CVT_S_W = 4,
  CVT_S_L = 4,
  CVT_D_W = 4,
  CVT_D_L = 4,

  CVT_S_D = 4,

  CVT_W_S = 4,
  CVT_W_D = 4,
  CVT_L_S = 4,
  CVT_L_D = 4,

  CEIL_W_S = 4,
  CEIL_W_D = 4,
  CEIL_L_S = 4,
  CEIL_L_D = 4,

  FLOOR_W_S = 4,
  FLOOR_W_D = 4,
  FLOOR_L_S = 4,
  FLOOR_L_D = 4,

  ROUND_W_S = 4,
  ROUND_W_D = 4,
  ROUND_L_S = 4,
  ROUND_L_D = 4,

  TRUNC_W_S = 4,
  TRUNC_W_D = 4,
  TRUNC_L_S = 4,
  TRUNC_L_D = 4,

  MOV_S = 4,
  MOV_D = 4,

  MOVF_S = 4,
  MOVF_D = 4,

  MOVN_S = 4,
  MOVN_D = 4,

  MOVT_S = 4,
  MOVT_D = 4,

  MOVZ_S = 4,
  MOVZ_D = 4,

  MUL_D = 5,
  MADD_D = 5,
  MSUB_D = 5,
  NMADD_D = 5,
  NMSUB_D = 5,

  RECIP_S = 13,
  RECIP_D = 26,

  RSQRT_S = 17,
  RSQRT_D = 36,

  DIV_S = 17,
  SQRT_S = 17,

  DIV_D = 32,
  SQRT_D = 32,

  MTC1 = 4,
  MTHC1 = 4,
  DMTC1 = 4,
  LWC1 = 4,
  LDC1 = 4,

  MFC1 = 1,
  MFHC1 = 1,
  DMFC1 = 1,
  MFHI = 1,
  MFLO = 1,
  SWC1 = 1,
  SDC1 = 1,
};

int DadduLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int DsubuLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int AndLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int OrLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int NorLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int XorLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int MulLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::MUL;
  } else {
    return Latency::MUL + 1;
  }
}

int DmulLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::DMUL;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::MUH;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhuLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::MUH;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DMulhLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::DMUH;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DivLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIV;
  } else {
    return Latency::DIV + 1;
  }
}

int DivuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIVU;
  } else {
    return Latency::DIVU + 1;
  }
}

int DdivLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::DDIV;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DdivuLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = Latency::DDIVU;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = 1;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModuLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = 1;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmodLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = 1;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmoduLatency(bool is_operand_register = true) {
  int latency = 0;
  latency = 1;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MovzLatency() { return Latency::BRANCH + 1; }

int MovnLatency() { return Latency::BRANCH + 1; }

int DlsaLatency() {
  // Estimated max.
  return DadduLatency() + 1;
}

int CallLatency() {
  // Estimated.
  return DadduLatency(false) + Latency::BRANCH + 5;
}

int JumpLatency() {
  // Estimated max.
  return 1 + DadduLatency() + Latency::BRANCH + 2;
}

int SmiUntagLatency() { return 1; }

int PrepareForTailCallLatency() {
  // Estimated max.
  return 2 * (DlsaLatency() + DadduLatency(false)) + 2 + Latency::BRANCH +
         Latency::BRANCH + 2 * DsubuLatency(false) + 2 + Latency::BRANCH + 1;
}

int AssemblePopArgumentsAdoptFrameLatency() {
  return 1 + Latency::BRANCH + 1 + SmiUntagLatency() +
         PrepareForTailCallLatency();
}

int AssertLatency() { return 1; }

int PrepareCallCFunctionLatency() {
  int frame_alignment = TurboAssembler::ActivationFrameAlignment();
  if (frame_alignment > kSystemPointerSize) {
    return 1 + DsubuLatency(false) + AndLatency(false) + 1;
  } else {
    return DsubuLatency(false);
  }
}

int AdjustBaseAndOffsetLatency() {
  return 3;  // Estimated max.
}

int AlignedMemoryLatency() { return AdjustBaseAndOffsetLatency() + 1; }

int UlhuLatency() { return AlignedMemoryLatency(); }

int UlwLatency() { return AlignedMemoryLatency(); }

int UlwuLatency() { return AlignedMemoryLatency(); }

int UldLatency() { return AlignedMemoryLatency(); }

int Ulwc1Latency() { return AlignedMemoryLatency(); }

int Uldc1Latency() { return AlignedMemoryLatency(); }

int UshLatency() { return AlignedMemoryLatency(); }

int UswLatency() { return AlignedMemoryLatency(); }

int UsdLatency() { return AlignedMemoryLatency(); }

int Uswc1Latency() { return AlignedMemoryLatency(); }

int Usdc1Latency() { return AlignedMemoryLatency(); }

int Lwc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LWC1; }

int Swc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SWC1; }

int Sdc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SDC1; }

int Ldc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LDC1; }

int MultiPushLatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency++;
  }
  return latency;
}

int MultiPushFPULatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency += Sdc1Latency();
  }
  return latency;
}

int PushCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPushLatency();
  if (fp_mode == kSaveFPRegs) {
    latency += MultiPushFPULatency();
  }
  return latency;
}

int MultiPopLatency() {
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency++;
  }
  return latency;
}

int MultiPopFPULatency() {
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency += Ldc1Latency();
  }
  return latency;
}

int PopCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPopLatency();
  if (fp_mode == kSaveFPRegs) {
    latency += MultiPopFPULatency();
  }
  return latency;
}

int CallCFunctionHelperLatency() {
  // Estimated.
  int latency = AndLatency(false) + Latency::BRANCH + 2 + CallLatency();
  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    latency++;
  } else {
    latency += DadduLatency(false);
  }
  return latency;
}

int CallCFunctionLatency() { return 1 + CallCFunctionHelperLatency(); }

int AssembleArchJumpLatency() {
  // Estimated max.
  return Latency::BRANCH;
}

int GenerateSwitchTableLatency() {
  int latency = 0;
  latency = DlsaLatency() + 2;
  latency += 2;
  return latency;
}

int AssembleArchTableSwitchLatency() {
  return Latency::BRANCH + GenerateSwitchTableLatency();
}

int DropAndRetLatency() {
  // Estimated max.
  return DadduLatency(false) + JumpLatency();
}

int AssemblerReturnLatency() {
  // Estimated max.
  return DadduLatency(false) + MultiPopLatency() + MultiPopFPULatency() +
         Latency::BRANCH + DadduLatency() + 1 + DropAndRetLatency();
}

int TryInlineTruncateDoubleToILatency() {
  return 2 + Latency::TRUNC_W_D + Latency::MFC1 + 2 + AndLatency(false) +
         Latency::BRANCH;
}

int CallStubDelayedLatency() { return 1 + CallLatency(); }

int TruncateDoubleToIDelayedLatency() {
  // TODO(loong64): This no longer reflects how TruncateDoubleToI is called.
  return TryInlineTruncateDoubleToILatency() + 1 + DsubuLatency(false) +
         Sdc1Latency() + CallStubDelayedLatency() + DadduLatency(false) + 1;
}

int CheckPageFlagLatency() {
  return AndLatency(false) + AlignedMemoryLatency() + AndLatency(false) +
         Latency::BRANCH;
}

int SltuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int BranchShortHelperLatency() {
  return 2;  // Estimated max.
}

int BranchShortLatency() { return BranchShortHelperLatency(); }

int MoveLatency() { return 1; }

int MovToFloatParametersLatency() { return 2 * MoveLatency(); }

int MovFromFloatResultLatency() { return MoveLatency(); }

int DaddOverflowLatency() {
  // Estimated max.
  return 6;
}

int DsubOverflowLatency() {
  // Estimated max.
  return 6;
}

int MulOverflowLatency() {
  // Estimated max.
  return MulLatency() + MulhLatency() + 2;
}

int DclzLatency() { return 1; }

int CtzLatency() { return 3 + DclzLatency(); }

int DctzLatency() { return 4; }

int PopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + MulLatency() + 1;
}

int DpopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + DmulLatency() + 1;
}

int CompareFLatency() { return Latency::C_cond_S; }

int CompareF32Latency() { return CompareFLatency(); }

int CompareF64Latency() { return CompareFLatency(); }

int CompareIsNanFLatency() { return CompareFLatency(); }

int CompareIsNanF32Latency() { return CompareIsNanFLatency(); }

int CompareIsNanF64Latency() { return CompareIsNanFLatency(); }

int NegsLatency() { return Latency::NEG_S; }

int NegdLatency() { return Latency::NEG_D; }

int Float64RoundLatency() { return Latency::RINT_D + 4; }

int Float32RoundLatency() { return Latency::RINT_S + 4; }

int Float32MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + Latency::MAX_S;
}

int Float64MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + Latency::MAX_D;
}

int Float32MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + Latency::MIN_S;
}

int Float64MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + Latency::MIN_D;
}

int TruncLSLatency(bool load_status) {
  int latency = Latency::TRUNC_L_S + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncLDLatency(bool load_status) {
  int latency = Latency::TRUNC_L_D + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncUlSLatency() {
  // Estimated max.
  return 2 * CompareF32Latency() + CompareIsNanF32Latency() +
         4 * Latency::BRANCH + Latency::SUB_S + 2 * Latency::TRUNC_L_S +
         3 * Latency::DMFC1 + OrLatency() + Latency::MTC1 + Latency::MOV_S +
         SltuLatency() + 4;
}

int TruncUlDLatency() {
  // Estimated max.
  return 2 * CompareF64Latency() + CompareIsNanF64Latency() +
         4 * Latency::BRANCH + Latency::SUB_D + 2 * Latency::TRUNC_L_D +
         3 * Latency::DMFC1 + OrLatency() + Latency::DMTC1 + Latency::MOV_D +
         SltuLatency() + 4;
}

int PushLatency() { return DadduLatency() + AlignedMemoryLatency(); }

int ByteSwapSignedLatency() { return 2; }

int LlLatency(int offset) {
  bool is_one_instruction = is_int14(offset);
  if (is_one_instruction) {
    return 1;
  } else {
    return 3;
  }
}

int ExtractBitsLatency(bool sign_extend, int size) {
  int latency = 2;
  if (sign_extend) {
    switch (size) {
      case 8:
      case 16:
      case 32:
        latency += 1;
        break;
      default:
        UNREACHABLE();
    }
  }
  return latency;
}

int InsertBitsLatency() { return 2 + DsubuLatency(false) + 2; }

int ScLatency(int offset) {
  bool is_one_instruction = is_int14(offset);
  if (is_one_instruction) {
    return 1;
  } else {
    return 3;
  }
}

int Word32AtomicExchangeLatency(bool sign_extend, int size) {
  return DadduLatency(false) + 1 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int Word32AtomicCompareExchangeLatency(bool sign_extend, int size) {
  return 2 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // Basic latency modeling for LOONG64 instructions. They have been determined
  // in empirical way.
  switch (instr->arch_opcode()) {
    case kArchCallCodeObject:
    case kArchCallWasmFunction:
      return CallLatency();
    case kArchTailCallCodeObjectFromJSFunction:
    case kArchTailCallCodeObject: {
      int latency = 0;
      if (instr->arch_opcode() == kArchTailCallCodeObjectFromJSFunction) {
        latency = AssemblePopArgumentsAdoptFrameLatency();
      }
      return latency + JumpLatency();
    }
    case kArchTailCallWasm:
    case kArchTailCallAddress:
      return JumpLatency();
    case kArchCallJSFunction: {
      int latency = 0;
      if (FLAG_debug_code) {
        latency = 1 + AssertLatency();
      }
      return latency + 1 + DadduLatency(false) + CallLatency();
    }
    case kArchPrepareCallCFunction:
      return PrepareCallCFunctionLatency();
    case kArchSaveCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PushCallerSavedLatency(fp_mode);
    }
    case kArchRestoreCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PopCallerSavedLatency(fp_mode);
    }
    case kArchPrepareTailCall:
      return 2;
    case kArchCallCFunction:
      return CallCFunctionLatency();
    case kArchJmp:
      return AssembleArchJumpLatency();
    case kArchTableSwitch:
      return AssembleArchTableSwitchLatency();
    case kArchAbortCSAAssert:
      return CallLatency() + 1;
    case kArchDebugBreak:
      return 1;
    case kArchComment:
    case kArchNop:
    case kArchThrowTerminator:
    case kArchDeoptimize:
      return 0;
    case kArchRet:
      return AssemblerReturnLatency();
    case kArchFramePointer:
      return 1;
    case kArchParentFramePointer:
      // Estimated max.
      return AlignedMemoryLatency();
    case kArchTruncateDoubleToI:
      return TruncateDoubleToIDelayedLatency();
    case kArchStoreWithWriteBarrier:
      return DadduLatency() + 1 + CheckPageFlagLatency();
    case kArchStackSlot:
      // Estimated max.
      return DadduLatency(false) + AndLatency(false) + AssertLatency() +
             DadduLatency(false) + AndLatency(false) + BranchShortLatency() +
             1 + DsubuLatency() + DadduLatency();
    case kArchWordPoisonOnSpeculation:
      return AndLatency();
    case kIeee754Float64Acos:
    case kIeee754Float64Acosh:
    case kIeee754Float64Asin:
    case kIeee754Float64Asinh:
    case kIeee754Float64Atan:
    case kIeee754Float64Atanh:
    case kIeee754Float64Atan2:
    case kIeee754Float64Cos:
    case kIeee754Float64Cosh:
    case kIeee754Float64Cbrt:
    case kIeee754Float64Exp:
    case kIeee754Float64Expm1:
    case kIeee754Float64Log:
    case kIeee754Float64Log1p:
    case kIeee754Float64Log10:
    case kIeee754Float64Log2:
    case kIeee754Float64Pow:
    case kIeee754Float64Sin:
    case kIeee754Float64Sinh:
    case kIeee754Float64Tan:
    case kIeee754Float64Tanh:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kLa64Add:
    case kLa64Dadd:
      return DadduLatency(instr->InputAt(1)->IsRegister());
    case kLa64DaddOvf:
      return DaddOverflowLatency();
    case kLa64Sub:
    case kLa64Dsub:
      return DsubuLatency(instr->InputAt(1)->IsRegister());
    case kLa64DsubOvf:
      return DsubOverflowLatency();
    case kLa64Mul:
      return MulLatency();
    case kLa64MulOvf:
      return MulOverflowLatency();
    case kLa64MulHigh:
      return MulhLatency();
    case kLa64MulHighU:
      return MulhuLatency();
    case kLa64DMulHigh:
      return DMulhLatency();
    case kLa64Div: {
      int latency = DivLatency(instr->InputAt(1)->IsRegister());
      return latency++;
    }
    case kLa64DivU: {
      int latency = DivuLatency(instr->InputAt(1)->IsRegister());
      return latency++;
    }
    case kLa64Mod:
      return ModLatency();
    case kLa64ModU:
      return ModuLatency();
    case kLa64Dmul:
      return DmulLatency();
    case kLa64Ddiv: {
      int latency = DdivLatency();
      return latency++;
    }
    case kLa64DdivU: {
      int latency = DdivuLatency();
      return latency++;
    }
    case kLa64Dmod:
      return DmodLatency();
    case kLa64DmodU:
      return DmoduLatency();
    case kLa64Dlsa:
    case kLa64Lsa:
      return DlsaLatency();
    case kLa64And:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kLa64And32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = AndLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kLa64Or:
      return OrLatency(instr->InputAt(1)->IsRegister());
    case kLa64Or32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = OrLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kLa64Nor:
      return NorLatency(instr->InputAt(1)->IsRegister());
    case kLa64Nor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = NorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kLa64Xor:
      return XorLatency(instr->InputAt(1)->IsRegister());
    case kLa64Xor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = XorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kLa64Clz:
    case kLa64Dclz:
      return DclzLatency();
    case kLa64Ctz:
      return CtzLatency();
    case kLa64Dctz:
      return DctzLatency();
    case kLa64Popcnt:
      return PopcntLatency();
    case kLa64Dpopcnt:
      return DpopcntLatency();
    case kLa64Shl:
      return 1;
    case kLa64Shr:
    case kLa64Sar:
      return 2;
    case kLa64Ext:
    case kLa64Ins:
    case kLa64Dext:
    case kLa64Dins:
    case kLa64Dshl:
    case kLa64Dshr:
    case kLa64Dsar:
    case kLa64Ror:
    case kLa64Dror:
      return 1;
    case kLa64Tst:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kLa64Mov:
      return 1;
    case kLa64CmpS:
      return MoveLatency() + CompareF32Latency();
    case kLa64AddS:
      return Latency::ADD_S;
    case kLa64SubS:
      return Latency::SUB_S;
    case kLa64MulS:
      return Latency::MUL_S;
    case kLa64DivS:
      return Latency::DIV_S;
    case kLa64ModS:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kLa64AbsS:
      return Latency::ABS_S;
    case kLa64NegS:
      return NegdLatency();
    case kLa64SqrtS:
      return Latency::SQRT_S;
    case kLa64MaxS:
      return Latency::MAX_S;
    case kLa64MinS:
      return Latency::MIN_S;
    case kLa64CmpD:
      return MoveLatency() + CompareF64Latency();
    case kLa64AddD:
      return Latency::ADD_D;
    case kLa64SubD:
      return Latency::SUB_D;
    case kLa64MulD:
      return Latency::MUL_D;
    case kLa64DivD:
      return Latency::DIV_D;
    case kLa64ModD:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kLa64AbsD:
      return Latency::ABS_D;
    case kLa64NegD:
      return NegdLatency();
    case kLa64SqrtD:
      return Latency::SQRT_D;
    case kLa64MaxD:
      return Latency::MAX_D;
    case kLa64MinD:
      return Latency::MIN_D;
    case kLa64Float64RoundDown:
    case kLa64Float64RoundTruncate:
    case kLa64Float64RoundUp:
    case kLa64Float64RoundTiesEven:
      return Float64RoundLatency();
    case kLa64Float32RoundDown:
    case kLa64Float32RoundTruncate:
    case kLa64Float32RoundUp:
    case kLa64Float32RoundTiesEven:
      return Float32RoundLatency();
    case kLa64Float32Max:
      return Float32MaxLatency();
    case kLa64Float64Max:
      return Float64MaxLatency();
    case kLa64Float32Min:
      return Float32MinLatency();
    case kLa64Float64Min:
      return Float64MinLatency();
    case kLa64Float64SilenceNaN:
      return Latency::SUB_D;
    case kLa64CvtSD:
      return Latency::CVT_S_D;
    case kLa64CvtDS:
      return Latency::CVT_D_S;
    case kLa64CvtDW:
      return Latency::MTC1 + Latency::CVT_D_W;
    case kLa64CvtSW:
      return Latency::MTC1 + Latency::CVT_S_W;
    case kLa64CvtSUw:
      return 1 + Latency::DMTC1 + Latency::CVT_S_L;
    case kLa64CvtSL:
      return Latency::DMTC1 + Latency::CVT_S_L;
    case kLa64CvtDL:
      return Latency::DMTC1 + Latency::CVT_D_L;
    case kLa64CvtDUw:
      return 1 + Latency::DMTC1 + Latency::CVT_D_L;
    case kLa64CvtDUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_D_L + Latency::ADD_D;
    case kLa64CvtSUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_S_L + Latency::ADD_S;
    case kLa64FloorWD:
      return Latency::FLOOR_W_D + Latency::MFC1;
    case kLa64CeilWD:
      return Latency::CEIL_W_D + Latency::MFC1;
    case kLa64RoundWD:
      return Latency::ROUND_W_D + Latency::MFC1;
    case kLa64TruncWD:
      return Latency::TRUNC_W_D + Latency::MFC1;
    case kLa64FloorWS:
      return Latency::FLOOR_W_S + Latency::MFC1;
    case kLa64CeilWS:
      return Latency::CEIL_W_S + Latency::MFC1;
    case kLa64RoundWS:
      return Latency::ROUND_W_S + Latency::MFC1;
    case kLa64TruncWS:
      return Latency::TRUNC_W_S + Latency::MFC1 + 2 + MovnLatency();
    case kLa64TruncLS:
      return TruncLSLatency(instr->OutputCount() > 1);
    case kLa64TruncLD:
      return TruncLDLatency(instr->OutputCount() > 1);
    case kLa64TruncUwD:
      // Estimated max.
      return CompareF64Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_D + Latency::SUB_D + OrLatency() +
             Latency::MTC1 + Latency::MFC1 + Latency::MTHC1 + 1;
    case kLa64TruncUwS:
      // Estimated max.
      return CompareF32Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_S + Latency::SUB_S + OrLatency() +
             Latency::MTC1 + 2 * Latency::MFC1 + 2 + MovzLatency();
    case kLa64TruncUlS:
      return TruncUlSLatency();
    case kLa64TruncUlD:
      return TruncUlDLatency();
    case kLa64BitcastDL:
      return Latency::DMFC1;
    case kLa64BitcastLD:
      return Latency::DMTC1;
    case kLa64Float64ExtractLowWord32:
      return Latency::MFC1;
    case kLa64Float64InsertLowWord32:
      return Latency::MFHC1 + Latency::MTC1 + Latency::MTHC1;
    case kLa64Float64ExtractHighWord32:
      return Latency::MFHC1;
    case kLa64Float64InsertHighWord32:
      return Latency::MTHC1;
    case kLa64Seb:
    case kLa64Seh:
      return 1;
    case kLa64Lbu:
    case kLa64Lb:
    case kLa64Lhu:
    case kLa64Lh:
    case kLa64Lwu:
    case kLa64Lw:
    case kLa64Ld:
    case kLa64Sb:
    case kLa64Sh:
    case kLa64Sw:
    case kLa64Sd:
      return AlignedMemoryLatency();
    case kLa64Lwc1:
      return Lwc1Latency();
    case kLa64Ldc1:
      return Ldc1Latency();
    case kLa64Swc1:
      return Swc1Latency();
    case kLa64Sdc1:
      return Sdc1Latency();
    case kLa64Ulhu:
    case kLa64Ulh:
      return UlhuLatency();
    case kLa64Ulwu:
      return UlwuLatency();
    case kLa64Ulw:
      return UlwLatency();
    case kLa64Uld:
      return UldLatency();
    case kLa64Ulwc1:
      return Ulwc1Latency();
    case kLa64Uldc1:
      return Uldc1Latency();
    case kLa64Ush:
      return UshLatency();
    case kLa64Usw:
      return UswLatency();
    case kLa64Usd:
      return UsdLatency();
    case kLa64Uswc1:
      return Uswc1Latency();
    case kLa64Usdc1:
      return Usdc1Latency();
    case kLa64Push: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        latency = Sdc1Latency() + DsubuLatency(false);
      } else {
        latency = PushLatency();
      }
      return latency;
    }
    case kLa64Peek: {
      int latency = 0;
      if (instr->OutputAt(0)->IsFPRegister()) {
        auto op = LocationOperand::cast(instr->OutputAt(0));
        switch (op->representation()) {
          case MachineRepresentation::kFloat64:
            latency = Ldc1Latency();
            break;
          case MachineRepresentation::kFloat32:
            latency = Latency::LWC1;
            break;
          default:
            UNREACHABLE();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kLa64StackClaim:
      return DsubuLatency(false);
    case kLa64StoreToStackSlot: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          latency = 1;  // Estimated value.
        } else {
          latency = Sdc1Latency();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kLa64ByteSwap64:
      return ByteSwapSignedLatency();
    case kLa64ByteSwap32:
      return ByteSwapSignedLatency();
    case kWord32AtomicLoadInt8:
    case kWord32AtomicLoadUint8:
    case kWord32AtomicLoadInt16:
    case kWord32AtomicLoadUint16:
    case kWord32AtomicLoadWord32:
      return 2;
    case kWord32AtomicStoreWord8:
    case kWord32AtomicStoreWord16:
    case kWord32AtomicStoreWord32:
      return 3;
    case kWord32AtomicExchangeInt8:
      return Word32AtomicExchangeLatency(true, 8);
    case kWord32AtomicExchangeUint8:
      return Word32AtomicExchangeLatency(false, 8);
    case kWord32AtomicExchangeInt16:
      return Word32AtomicExchangeLatency(true, 16);
    case kWord32AtomicExchangeUint16:
      return Word32AtomicExchangeLatency(false, 16);
    case kWord32AtomicExchangeWord32:
      return 2 + LlLatency(0) + 1 + ScLatency(0) + BranchShortLatency() + 1;
    case kWord32AtomicCompareExchangeInt8:
      return Word32AtomicCompareExchangeLatency(true, 8);
    case kWord32AtomicCompareExchangeUint8:
      return Word32AtomicCompareExchangeLatency(false, 8);
    case kWord32AtomicCompareExchangeInt16:
      return Word32AtomicCompareExchangeLatency(true, 16);
    case kWord32AtomicCompareExchangeUint16:
      return Word32AtomicCompareExchangeLatency(false, 16);
    case kWord32AtomicCompareExchangeWord32:
      return 3 + LlLatency(0) + BranchShortLatency() + 1 + ScLatency(0) +
             BranchShortLatency() + 1;
    case kLa64AssertEqual:
      return AssertLatency();
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
