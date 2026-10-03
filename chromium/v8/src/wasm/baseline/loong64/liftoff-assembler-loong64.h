// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_H_
#define V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_H_

#include "src/wasm/baseline/liftoff-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace liftoff {

// Liftoff Frames.
//
//  slot      Frame
//       +--------------------+---------------------------
//  n+4  | optional padding slot to keep the stack 16 byte aligned.
//  n+3  |   parameter n      |
//  ...  |       ...          |
//   4   |   parameter 1      | or parameter 2
//   3   |   parameter 0      | or parameter 1
//   2   |  (result address)  | or parameter 0
//  -----+--------------------+---------------------------
//   1   | return addr (ra)   |
//   0   | previous frame (fp)|
//  -----+--------------------+  <-- frame ptr (fp)
//  -1   | 0xa: WASM          |
//  -2   |     instance       |
//  -----+--------------------+---------------------------
//  -3   |     slot 0         |   ^
//  -4   |     slot 1         |   |
//       |                    | Frame slots
//       |                    |   |
//       |                    |   v
//       | optional padding slot to keep the stack 16 byte aligned.
//  -----+--------------------+  <-- stack ptr (sp)
//

// fp-8 holds the stack marker, fp-16 is the instance parameter.
constexpr int kInstanceOffset = 16;

inline MemOperand GetStackSlot(int offset) { return MemOperand(fp, -offset); }

inline MemOperand GetInstanceOperand() { return GetStackSlot(kInstanceOffset); }

inline MemOperand GetMemOp(LiftoffAssembler* assm, Register addr,
                           Register offset, uint32_t offset_imm) {
  if (is_uint31(offset_imm)) {
    if (offset == no_reg) return MemOperand(addr, offset_imm);
    assm->add_d(kScratchReg, addr, offset);
    return MemOperand(kScratchReg, offset_imm);
  }
  // Offset immediate does not fit in 31 bits.
  assm->li(kScratchReg, Operand(offset_imm));
  assm->add_d(kScratchReg, kScratchReg, addr);
  if (offset != no_reg) {
    assm->add_d(kScratchReg, kScratchReg, offset);
  }
  return MemOperand(kScratchReg, 0);
}

inline void Load(LiftoffAssembler* assm, LiftoffRegister dst, MemOperand src,
                 ValueType type) {
  switch (type.kind()) {
    case ValueType::kI32:
      assm->Ld_w(dst.gp(), src);
      break;
    case ValueType::kI64:
      assm->Ld_d(dst.gp(), src);
      break;
    case ValueType::kF32:
      assm->Fld_s(dst.fp(), src);
      break;
    case ValueType::kF64:
      assm->Fld_d(dst.fp(), src);
      break;
    case ValueType::kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

inline void Store(LiftoffAssembler* assm, Register base, int32_t offset,
                  LiftoffRegister src, ValueType type) {
  MemOperand dst(base, offset);
  switch (type.kind()) {
    case ValueType::kI32:
      assm->St_w(src.gp(), dst);
      break;
    case ValueType::kI64:
      assm->St_d(src.gp(), dst);
      break;
    case ValueType::kF32:
      assm->Fst_s(src.fp(), dst);
      break;
    case ValueType::kF64:
      assm->Fst_d(src.fp(), dst);
      break;
    default:
      UNREACHABLE();
  }
}

inline void push(LiftoffAssembler* assm, LiftoffRegister reg, ValueType type) {
  switch (type.kind()) {
    case ValueType::kI32:
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->St_w(reg.gp(), MemOperand(sp, 0));
      break;
    case ValueType::kI64:
      assm->push(reg.gp());
      break;
    case ValueType::kF32:
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->Fst_s(reg.fp(), MemOperand(sp, 0));
      break;
    case ValueType::kF64:
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->Fst_d(reg.fp(), MemOperand(sp, 0));
      break;
    case ValueType::kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  // When constant that represents size of stack frame can't be represented
  // as 16bit we need three instructions to add it to sp, so we reserve space
  // for this case.
  addi_d(sp, sp, 0);
  nop();
  nop();
  return offset;
}

void LiftoffAssembler::PrepareTailCall(int num_callee_stack_params,
                                       int stack_param_delta) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Push the return address and frame pointer to complete the stack frame.
  Ld_d(scratch, MemOperand(fp, 8));
  Push(scratch);
  Ld_d(scratch, MemOperand(fp, 0));
  Push(scratch);

  // Shift the whole frame upwards.
  int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    Ld_d(scratch, MemOperand(sp, i * 8));
    St_d(scratch, MemOperand(fp, (i - stack_param_delta) * 8));
  }

  // Set the new stack and frame pointer.
  addi_d(sp, fp, -stack_param_delta * 8);
  Pop(ra, fp);
}

void LiftoffAssembler::PatchPrepareStackFrame(int offset, int frame_size) {
  // We can't run out of space, just pass anything big enough to not cause the
  // assembler to try to grow the buffer.
  constexpr int kAvailableSpace = 256;
  TurboAssembler patching_assembler(
      nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
      ExternalAssemblerBuffer(buffer_start_ + offset, kAvailableSpace));
  // If bytes can be represented as 16bit, daddiu will be generated and two
  // nops will stay untouched. Otherwise, lui-ori sequence will load it to
  // register and, as third instruction, daddu will be generated.
  patching_assembler.Add_d(sp, sp, Operand(-frame_size));
}

void LiftoffAssembler::FinishCode() {}

void LiftoffAssembler::AbortCompilation() {}

// static
constexpr int LiftoffAssembler::StaticStackFrameSize() {
  return liftoff::kInstanceOffset;
}

int LiftoffAssembler::SlotSizeForType(ValueType type) {
  switch (type.kind()) {
    case ValueType::kS128:
      return type.element_size_bytes();
    default:
      return kStackSlotSize;
  }
}

bool LiftoffAssembler::NeedsAlignment(ValueType type) {
  return type.kind() == ValueType::kS128 || type.is_reference_type();
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value,
                                    RelocInfo::Mode rmode) {
  switch (value.type().kind()) {
    case ValueType::kI32:
      TurboAssembler::li(reg.gp(), Operand(value.to_i32(), rmode));
      break;
    case ValueType::kI64:
      TurboAssembler::li(reg.gp(), Operand(value.to_i64(), rmode));
      break;
    case ValueType::kF32:
      TurboAssembler::Move(reg.fp(), value.to_f32_boxed().get_bits());
      break;
    case ValueType::kF64:
      TurboAssembler::Move(reg.fp(), value.to_f64_boxed().get_bits());
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadFromInstance(Register dst, uint32_t offset,
                                        int size) {
  DCHECK_LE(offset, kMaxInt);
  Ld_d(dst, liftoff::GetInstanceOperand());
  DCHECK(size == 4 || size == 8);
  if (size == 4) {
    Ld_w(dst, MemOperand(dst, offset));
  } else {
    Ld_d(dst, MemOperand(dst, offset));
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     uint32_t offset) {
  LoadFromInstance(dst, offset, kTaggedSize);
}

void LiftoffAssembler::SpillInstance(Register instance) {
  St_d(instance, liftoff::GetInstanceOperand());
}

void LiftoffAssembler::FillInstanceInto(Register dst) {
  Ld_d(dst, liftoff::GetInstanceOperand());
}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         LiftoffRegList pinned) {
  DCHECK_GE(offset_imm, 0);
  STATIC_ASSERT(kTaggedSize == kInt64Size);
  Load(LiftoffRegister(dst), src_addr, offset_reg,
       static_cast<uint32_t>(offset_imm), LoadType::kI64Load, pinned);
}

void LiftoffAssembler::StoreTaggedPointer(Register dst_addr, int32_t offset_imm,
                                          LiftoffRegister src,
                                          LiftoffRegList pinned) {
  bailout(kRefTypes, "GlobalSet");
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uint32_t offset_imm,
                            LoadType type, LiftoffRegList pinned,
                            uint32_t* protected_load_pc, bool is_load_mem) {
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm);

  if (protected_load_pc) *protected_load_pc = pc_offset();
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      Ld_bu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load8S:
    case LoadType::kI64Load8S:
      Ld_b(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      TurboAssembler::Ld_hu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      TurboAssembler::Ld_h(dst.gp(), src_op);
      break;
    case LoadType::kI64Load32U:
      TurboAssembler::Ld_wu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      TurboAssembler::Ld_w(dst.gp(), src_op);
      break;
    case LoadType::kI64Load:
      TurboAssembler::Ld_d(dst.gp(), src_op);
      break;
    case LoadType::kF32Load:
      TurboAssembler::Fld_s(dst.fp(), src_op);
      break;
    case LoadType::kF64Load:
      TurboAssembler::Fld_d(dst.fp(), src_op);
      break;
    case LoadType::kS128Load:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uint32_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem) {
  MemOperand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);

  if (protected_store_pc) *protected_store_pc = pc_offset();
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      St_b(src.gp(), dst_op);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      TurboAssembler::St_h(src.gp(), dst_op);
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      TurboAssembler::St_w(src.gp(), dst_op);
      break;
    case StoreType::kI64Store:
      TurboAssembler::St_d(src.gp(), dst_op);
      break;
    case StoreType::kF32Store:
      TurboAssembler::Fst_s(src.fp(), dst_op);
      break;
    case StoreType::kF64Store:
      TurboAssembler::Fst_d(src.fp(), dst_op);
      break;
    case StoreType::kS128Store:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicLoad(LiftoffRegister dst, Register src_addr,
                                  Register offset_reg, uint32_t offset_imm,
                                  LoadType type, LiftoffRegList pinned) {
  bailout(kAtomics, "AtomicLoad");
}

void LiftoffAssembler::AtomicStore(Register dst_addr, Register offset_reg,
                                   uint32_t offset_imm, LiftoffRegister src,
                                   StoreType type, LiftoffRegList pinned) {
  bailout(kAtomics, "AtomicStore");
}

void LiftoffAssembler::AtomicAdd(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicAdd");
}

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicSub");
}

void LiftoffAssembler::AtomicAnd(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicAnd");
}

void LiftoffAssembler::AtomicOr(Register dst_addr, Register offset_reg,
                                uint32_t offset_imm, LiftoffRegister value,
                                LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicOr");
}

void LiftoffAssembler::AtomicXor(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicXor");
}

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uint32_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type) {
  bailout(kAtomics, "AtomicExchange");
}

void LiftoffAssembler::AtomicCompareExchange(
    Register dst_addr, Register offset_reg, uint32_t offset_imm,
    LiftoffRegister expected, LiftoffRegister new_value, LiftoffRegister result,
    StoreType type) {
  bailout(kAtomics, "AtomicCompareExchange");
}

void LiftoffAssembler::AtomicFence() { dbar(0); }

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueType type) {
  MemOperand src(fp, kSystemPointerSize * (caller_slot_idx + 1));
  liftoff::Load(this, dst, src, type);
}

void LiftoffAssembler::StoreCallerFrameSlot(LiftoffRegister src,
                                            uint32_t caller_slot_idx,
                                            ValueType type) {
  int32_t offset = kSystemPointerSize * (caller_slot_idx + 1);
  liftoff::Store(this, fp, offset, src, type);
}

void LiftoffAssembler::LoadReturnStackSlot(LiftoffRegister dst, int offset,
                                           ValueType type) {
  liftoff::Load(this, dst, MemOperand(sp, offset), type);
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_offset, uint32_t src_offset,
                                      ValueType type) {
  DCHECK_NE(dst_offset, src_offset);
  LiftoffRegister reg = GetUnusedRegister(reg_class_for(type), {});
  Fill(reg, src_offset, type);
  Spill(dst_offset, reg, type);
}

void LiftoffAssembler::Move(Register dst, Register src, ValueType type) {
  DCHECK_NE(dst, src);
  // TODO(ksreten): Handle different sizes here.
  TurboAssembler::Move(dst, src);
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueType type) {
  DCHECK_NE(dst, src);
  if (type != kWasmS128) {
    TurboAssembler::Move(dst, src);
  } else {
    UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, LiftoffRegister reg, ValueType type) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  switch (type.kind()) {
    case ValueType::kI32:
      St_w(reg.gp(), dst);
      break;
    case ValueType::kI64:
    case ValueType::kRef:
    case ValueType::kOptRef:
      St_d(reg.gp(), dst);
      break;
    case ValueType::kF32:
      Fst_s(reg.fp(), dst);
      break;
    case ValueType::kF64:
      TurboAssembler::Fst_d(reg.fp(), dst);
      break;
    case ValueType::kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, WasmValue value) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  switch (value.type().kind()) {
    case ValueType::kI32: {
      LiftoffRegister tmp = GetUnusedRegister(kGpReg, {});
      TurboAssembler::li(tmp.gp(), Operand(value.to_i32()));
      St_w(tmp.gp(), dst);
      break;
    }
    case ValueType::kI64:
    case ValueType::kRef:
    case ValueType::kOptRef: {
      LiftoffRegister tmp = GetUnusedRegister(kGpReg, {});
      TurboAssembler::li(tmp.gp(), value.to_i64());
      St_d(tmp.gp(), dst);
      break;
    }
    default:
      // kWasmF32 and kWasmF64 are unreachable, since those
      // constants are not tracked.
      UNREACHABLE();
  }
}

void LiftoffAssembler::Fill(LiftoffRegister reg, int offset, ValueType type) {
  MemOperand src = liftoff::GetStackSlot(offset);
  switch (type.kind()) {
    case ValueType::kI32:
      Ld_w(reg.gp(), src);
      break;
    case ValueType::kI64:
    case ValueType::kRef:
    case ValueType::kOptRef:
      Ld_d(reg.gp(), src);
      break;
    case ValueType::kF32:
      Fld_s(reg.fp(), src);
      break;
    case ValueType::kF64:
      TurboAssembler::Fld_d(reg.fp(), src);
      break;
    case ValueType::kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::FillI64Half(Register, int offset, RegPairHalf) {
  UNREACHABLE();
}

void LiftoffAssembler::FillStackSlotsWithZero(int start, int size) {
  DCHECK_LT(0, size);
  RecordUsedSpillOffset(start + size);

  if (size <= 12 * kStackSlotSize) {
    // Special straight-line code for up to 12 slots. Generates one
    // instruction per slot (<= 12 instructions total).
    uint32_t remainder = size;
    for (; remainder >= kStackSlotSize; remainder -= kStackSlotSize) {
      St_d(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      St_w(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
  } else {
    // General case for bigger counts (12 instructions).
    // Use a0 for start address (inclusive), a1 for end address (exclusive).
    Push(a1, a0);
    Add_d(a0, fp, Operand(-start - size));
    Add_d(a1, fp, Operand(-start));

    Label loop;
    bind(&loop);
    St_d(zero_reg, MemOperand(a0, 0));
    addi_d(a0, a0, kSystemPointerSize);
    BranchShort(&loop, ne, a0, Operand(a1));

    Pop(a1, a0);
  }
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  TurboAssembler::Clz_d(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  TurboAssembler::Ctz_d(dst.gp(), src.gp());
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  TurboAssembler::Popcnt_d(dst.gp(), src.gp());
  return true;
}

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  TurboAssembler::Mul_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));

  // Check if lhs == kMinInt and rhs == -1, since this case is unrepresentable.
  TurboAssembler::li(kScratchReg, 1);
  TurboAssembler::li(kScratchReg2, 1);
  TurboAssembler::LoadZeroOnCondition(kScratchReg, lhs, Operand(kMinInt), eq);
  TurboAssembler::LoadZeroOnCondition(kScratchReg2, rhs, Operand(-1), eq);
  add_d(kScratchReg, kScratchReg, kScratchReg2);
  TurboAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  TurboAssembler::Div_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  TurboAssembler::Div_wu(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  TurboAssembler::Mod_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  TurboAssembler::Mod_wu(dst, lhs, rhs);
}

#define I32_BINOP(name, instruction)                                 \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    instruction(dst, lhs, rhs);                                      \
  }

// clang-format off
I32_BINOP(add, add_w)
I32_BINOP(sub, sub_w)
I32_BINOP(and, and_)
I32_BINOP(or, or_)
I32_BINOP(xor, xor_)
// clang-format on

#undef I32_BINOP

#define I32_BINOP_I(name, instruction)                                  \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register lhs, \
                                            int32_t imm) {              \
    instruction(dst, lhs, imm);                                         \
  }

// clang-format off
I32_BINOP_I(add, Add_w)
I32_BINOP_I(and, And)
I32_BINOP_I(or, Or)
I32_BINOP_I(xor, Xor)
// clang-format on

#undef I32_BINOP_I

void LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  TurboAssembler::Clz_w(dst, src);
}

void LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  TurboAssembler::Ctz_w(dst, src);
}

bool LiftoffAssembler::emit_i32_popcnt(Register dst, Register src) {
  TurboAssembler::Popcnt_w(dst, src);
  return true;
}

#define I32_SHIFTOP(name, instruction)                               \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register src, \
                                         Register amount) {          \
    instruction(dst, src, amount);                                   \
  }
#define I32_SHIFTOP_I(name, instruction, instruction1)                  \
  I32_SHIFTOP(name, instruction)                                        \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register src, \
                                            int amount) {               \
    instruction1(dst, src, amount & 0x1f);                              \
  }

I32_SHIFTOP_I(shl, sll_w, slli_w)
I32_SHIFTOP_I(sar, sra_w, srai_w)
I32_SHIFTOP_I(shr, srl_w, srli_w)

#undef I32_SHIFTOP
#undef I32_SHIFTOP_I

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  TurboAssembler::Mul_d(dst.gp(), lhs.gp(), rhs.gp());
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));

  // Check if lhs == MinInt64 and rhs == -1, since this case is unrepresentable.
  TurboAssembler::li(kScratchReg, 1);
  TurboAssembler::li(kScratchReg2, 1);
  TurboAssembler::LoadZeroOnCondition(
      kScratchReg, lhs.gp(), Operand(std::numeric_limits<int64_t>::min()), eq);
  TurboAssembler::LoadZeroOnCondition(kScratchReg2, rhs.gp(), Operand(-1), eq);
  add_d(kScratchReg, kScratchReg, kScratchReg2);
  TurboAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  TurboAssembler::Div_d(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  TurboAssembler::Div_du(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  TurboAssembler::Mod_d(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  TurboAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  TurboAssembler::Mod_du(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

#define I64_BINOP(name, instruction)                                   \
  void LiftoffAssembler::emit_i64_##name(                              \
      LiftoffRegister dst, LiftoffRegister lhs, LiftoffRegister rhs) { \
    instruction(dst.gp(), lhs.gp(), rhs.gp());                         \
  }

// clang-format off
I64_BINOP(add, Add_d)
I64_BINOP(sub, Sub_d)
I64_BINOP(and, and_)
I64_BINOP(or, or_)
I64_BINOP(xor, xor_)
// clang-format on

#undef I64_BINOP

#define I64_BINOP_I(name, instruction)                         \
  void LiftoffAssembler::emit_i64_##name##i(                   \
      LiftoffRegister dst, LiftoffRegister lhs, int32_t imm) { \
    instruction(dst.gp(), lhs.gp(), imm);                      \
  }

// clang-format off
I64_BINOP_I(add, addi_d)
I64_BINOP_I(and, And)
I64_BINOP_I(or, Or)
I64_BINOP_I(xor, Xor)
// clang-format on

#undef I64_BINOP_I

#define I64_SHIFTOP(name, instruction)                             \
  void LiftoffAssembler::emit_i64_##name(                          \
      LiftoffRegister dst, LiftoffRegister src, Register amount) { \
    instruction(dst.gp(), src.gp(), amount);                       \
  }
#define I64_SHIFTOP_I(name, instruction, instructioni)                         \
  I64_SHIFTOP(name, instruction)                                               \
  void LiftoffAssembler::emit_i64_##name##i(LiftoffRegister dst,               \
                                            LiftoffRegister src, int amount) { \
    instructioni(dst.gp(), src.gp(), amount & 63);                             \
  }

I64_SHIFTOP_I(shl, sll_d, slli_d)
I64_SHIFTOP_I(sar, sra_d, srai_d)
I64_SHIFTOP_I(shr, srl_d, srli_d)

#undef I64_SHIFTOP
#undef I64_SHIFTOP_I

void LiftoffAssembler::emit_u32_to_intptr(Register dst, Register src) {
  bstrpick_d(dst, src, 31, 0);
}

void LiftoffAssembler::emit_f32_neg(DoubleRegister dst, DoubleRegister src) {
  TurboAssembler::Neg_s(dst, src);
}

void LiftoffAssembler::emit_f64_neg(DoubleRegister dst, DoubleRegister src) {
  TurboAssembler::Neg_d(dst, src);
}

void LiftoffAssembler::emit_f32_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  TurboAssembler::Float32Min(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  TurboAssembler::Float32MinOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f32_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  TurboAssembler::Float32Max(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  TurboAssembler::Float32MaxOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f32_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  bailout(kComplexOperation, "f32_copysign");
}

void LiftoffAssembler::emit_f64_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  TurboAssembler::Float64Min(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  TurboAssembler::Float64MinOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f64_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  TurboAssembler::Float64Max(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  TurboAssembler::Float64MaxOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f64_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  bailout(kComplexOperation, "f64_copysign");
}

#define FP_BINOP(name, instruction)                                          \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister lhs, \
                                     DoubleRegister rhs) {                   \
    instruction(dst, lhs, rhs);                                              \
  }
#define FP_UNOP(name, instruction)                                             \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    instruction(dst, src);                                                     \
  }
#define FP_UNOP_RETURN_TRUE(name, instruction)                                 \
  bool LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    instruction(dst, src);                                                     \
    return true;                                                               \
  }

FP_BINOP(f32_add, fadd_s)
FP_BINOP(f32_sub, fsub_s)
FP_BINOP(f32_mul, fmul_s)
FP_BINOP(f32_div, fdiv_s)
FP_UNOP(f32_abs, fabs_s)
FP_UNOP_RETURN_TRUE(f32_ceil, Ceil_s)
FP_UNOP_RETURN_TRUE(f32_floor, Floor_s)
FP_UNOP_RETURN_TRUE(f32_trunc, Trunc_s)
FP_UNOP_RETURN_TRUE(f32_nearest_int, Round_s)
FP_UNOP(f32_sqrt, fsqrt_s)
FP_BINOP(f64_add, fadd_d)
FP_BINOP(f64_sub, fsub_d)
FP_BINOP(f64_mul, fmul_d)
FP_BINOP(f64_div, fdiv_d)
FP_UNOP(f64_abs, fabs_d)
FP_UNOP_RETURN_TRUE(f64_ceil, Ceil_d)
FP_UNOP_RETURN_TRUE(f64_floor, Floor_d)
FP_UNOP_RETURN_TRUE(f64_trunc, Trunc_d)
FP_UNOP_RETURN_TRUE(f64_nearest_int, Round_d)
FP_UNOP(f64_sqrt, fsqrt_d)

#undef FP_BINOP
#undef FP_UNOP
#undef FP_UNOP_RETURN_TRUE

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      TurboAssembler::bstrpick_w(dst.gp(), src.gp(), 31, 0);
      return true;
    case kExprI32SConvertF32: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_s(rounded.fp(), src.fp());
      ftintrz_w_s(kScratchDoubleReg, rounded.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);
      // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
      // because INT32_MIN allows easier out-of-bounds detection.
      TurboAssembler::Add_w(kScratchReg, dst.gp(), 1);
      TurboAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      TurboAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_w(kScratchDoubleReg, dst.gp());
      ffint_s_w(converted_back.fp(), kScratchDoubleReg);
      TurboAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF32: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_s(rounded.fp(), src.fp());
      TurboAssembler::Ftintrz_uw_s(dst.gp(), rounded.fp(), kScratchDoubleReg);
      // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
      // because 0 allows easier out-of-bounds detection.
      TurboAssembler::Add_w(kScratchReg, dst.gp(), 1);
      TurboAssembler::Movz(dst.gp(), zero_reg, kScratchReg);

      // Checking if trap.
      TurboAssembler::Ffint_d_uw(converted_back.fp(), dst.gp());
      fcvt_s_d(converted_back.fp(), converted_back.fp());
      TurboAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32SConvertF64: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_d(rounded.fp(), src.fp());
      ftintrz_w_d(kScratchDoubleReg, rounded.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);

      // Checking if trap.
      ffint_d_w(converted_back.fp(), kScratchDoubleReg);
      TurboAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF64: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_d(rounded.fp(), src.fp());
      TurboAssembler::Ftintrz_uw_d(dst.gp(), rounded.fp(), kScratchDoubleReg);

      // Checking if trap.
      TurboAssembler::Ffint_d_uw(converted_back.fp(), dst.gp());
      TurboAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32ReinterpretF32:
      TurboAssembler::FmoveLow(dst.gp(), src.fp());
      return true;
    case kExprI64SConvertI32:
      slli_w(dst.gp(), src.gp(), 0);
      return true;
    case kExprI64UConvertI32:
      TurboAssembler::bstrpick_d(dst.gp(), src.gp(), 31, 0);
      return true;
    case kExprI64SConvertF32: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_s(rounded.fp(), src.fp());
      ftintrz_l_s(kScratchDoubleReg, rounded.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      TurboAssembler::Add_d(kScratchReg, dst.gp(), 1);
      TurboAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      TurboAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_d(kScratchDoubleReg, dst.gp());
      ffint_s_l(converted_back.fp(), kScratchDoubleReg);
      TurboAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF32: {
      // Real conversion.
      TurboAssembler::Ftintrz_ul_s(dst.gp(), src.fp(), kScratchDoubleReg,
                                   kScratchReg);

      // Checking if trap.
      TurboAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      return true;
    }
    case kExprI64SConvertF64: {
      LiftoffRegister rounded =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src));
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(src, rounded));

      // Real conversion.
      TurboAssembler::Trunc_d(rounded.fp(), src.fp());
      ftintrz_l_d(kScratchDoubleReg, rounded.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      TurboAssembler::Add_d(kScratchReg, dst.gp(), 1);
      TurboAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      TurboAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_d(kScratchDoubleReg, dst.gp());
      ffint_d_l(converted_back.fp(), kScratchDoubleReg);
      TurboAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      TurboAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF64: {
      // Real conversion.
      TurboAssembler::Ftintrz_ul_d(dst.gp(), src.fp(), kScratchDoubleReg,
                                   kScratchReg);

      // Checking if trap.
      TurboAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      return true;
    }
    case kExprI64ReinterpretF64:
      movfr2gr_d(dst.gp(), src.fp());
      return true;
    case kExprF32SConvertI32: {
      LiftoffRegister scratch =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(dst));
      movgr2fr_w(scratch.fp(), src.gp());
      ffint_s_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF32UConvertI32:
      TurboAssembler::Ffint_s_uw(dst.fp(), src.gp());
      return true;
    case kExprF32ConvertF64:
      fcvt_s_d(dst.fp(), src.fp());
      return true;
    case kExprF32ReinterpretI32:
      TurboAssembler::FmoveLow(dst.fp(), src.gp());
      return true;
    case kExprF64SConvertI32: {
      LiftoffRegister scratch =
          GetUnusedRegister(kFpReg, LiftoffRegList::ForRegs(dst));
      movgr2fr_w(scratch.fp(), src.gp());
      ffint_d_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF64UConvertI32:
      TurboAssembler::Ffint_d_uw(dst.fp(), src.gp());
      return true;
    case kExprF64ConvertF32:
      fcvt_d_s(dst.fp(), src.fp());
      return true;
    case kExprF64ReinterpretI64:
      movgr2fr_d(dst.fp(), src.gp());
      return true;
    case kExprI32SConvertSatF32:
      bailout(kNonTrappingFloatToInt, "kExprI32SConvertSatF32");
      return true;
    case kExprI32UConvertSatF32:
      bailout(kNonTrappingFloatToInt, "kExprI32UConvertSatF32");
      return true;
    case kExprI32SConvertSatF64:
      bailout(kNonTrappingFloatToInt, "kExprI32SConvertSatF64");
      return true;
    case kExprI32UConvertSatF64:
      bailout(kNonTrappingFloatToInt, "kExprI32UConvertSatF64");
      return true;
    case kExprI64SConvertSatF32:
      bailout(kNonTrappingFloatToInt, "kExprI64SConvertSatF32");
      return true;
    case kExprI64UConvertSatF32:
      bailout(kNonTrappingFloatToInt, "kExprI64UConvertSatF32");
      return true;
    case kExprI64SConvertSatF64:
      bailout(kNonTrappingFloatToInt, "kExprI64SConvertSatF64");
      return true;
    case kExprI64UConvertSatF64:
      bailout(kNonTrappingFloatToInt, "kExprI64UConvertSatF64");
      return true;
    default:
      return false;
  }
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  bailout(kComplexOperation, "i32_signextend_i8");
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  bailout(kComplexOperation, "i32_signextend_i16");
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  bailout(kComplexOperation, "i64_signextend_i8");
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  bailout(kComplexOperation, "i64_signextend_i16");
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  bailout(kComplexOperation, "i64_signextend_i32");
}

void LiftoffAssembler::emit_jump(Label* label) {
  TurboAssembler::Branch(label);
}

void LiftoffAssembler::emit_jump(Register target) {
  TurboAssembler::Jump(target);
}

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueType type, Register lhs,
                                      Register rhs) {
  if (rhs != no_reg) {
    TurboAssembler::Branch(label, cond, lhs, Operand(rhs));
  } else {
    TurboAssembler::Branch(label, cond, lhs, Operand(zero_reg));
  }
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  sltui(dst, src, 1);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  Register tmp = dst;
  if (dst == lhs || dst == rhs) {
    tmp = GetUnusedRegister(kGpReg, LiftoffRegList::ForRegs(lhs, rhs)).gp();
  }
  // Write 1 as result.
  TurboAssembler::li(tmp, 1);

  // If negative condition is true, write 0 as result.
  Condition neg_cond = NegateCondition(cond);
  TurboAssembler::LoadZeroOnCondition(tmp, lhs, Operand(rhs), neg_cond);

  // If tmp != dst, result will be moved.
  TurboAssembler::Move(dst, tmp);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  sltui(dst, src.gp(), 1);
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  Register tmp = dst;
  if (dst == lhs.gp() || dst == rhs.gp()) {
    tmp = GetUnusedRegister(kGpReg, LiftoffRegList::ForRegs(lhs, rhs)).gp();
  }
  // Write 1 as result.
  TurboAssembler::li(tmp, 1);

  // If negative condition is true, write 0 as result.
  Condition neg_cond = NegateCondition(cond);
  TurboAssembler::LoadZeroOnCondition(tmp, lhs.gp(), Operand(rhs.gp()),
                                      neg_cond);

  // If tmp != dst, result will be moved.
  TurboAssembler::Move(dst, tmp);
}

namespace liftoff {

inline FPUCondition ConditionToConditionCmpFPU(Condition condition,
                                               bool* predicate) {
  switch (condition) {
    case kEqual:
      *predicate = true;
      return CEQ;
    case kUnequal:
      *predicate = false;
      return CEQ;
    case kUnsignedLessThan:
      *predicate = true;
      return CLT;
    case kUnsignedGreaterEqual:
      *predicate = false;
      return CLT;
    case kUnsignedLessEqual:
      *predicate = true;
      return CLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return CLE;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
}

inline void EmitAnyTrue(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister src) {}
}  // namespace liftoff

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  Label not_nan, cont;
  TurboAssembler::CompareIsNanF32(lhs, rhs);
  TurboAssembler::BranchFalseF(&not_nan);
  // If one of the operands is NaN, return 1 for f32.ne, else 0.
  if (cond == ne) {
    TurboAssembler::li(dst, 1);
  } else {
    TurboAssembler::Move(dst, zero_reg);
  }
  TurboAssembler::Branch(&cont);

  bind(&not_nan);

  TurboAssembler::li(dst, 1);
  bool predicate;
  FPUCondition fcond = liftoff::ConditionToConditionCmpFPU(cond, &predicate);
  TurboAssembler::CompareF32(lhs, rhs, fcond);
  if (predicate) {
    TurboAssembler::LoadZeroIfNotFPUCondition(dst);
  } else {
    TurboAssembler::LoadZeroIfFPUCondition(dst);
  }

  bind(&cont);
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  Label not_nan, cont;
  TurboAssembler::CompareIsNanF64(lhs, rhs);
  TurboAssembler::BranchFalseF(&not_nan);
  // If one of the operands is NaN, return 1 for f64.ne, else 0.
  if (cond == ne) {
    TurboAssembler::li(dst, 1);
  } else {
    TurboAssembler::Move(dst, zero_reg);
  }
  TurboAssembler::Branch(&cont);

  bind(&not_nan);

  TurboAssembler::li(dst, 1);
  bool predicate;
  FPUCondition fcond = liftoff::ConditionToConditionCmpFPU(cond, &predicate);
  TurboAssembler::CompareF64(lhs, rhs, fcond);
  if (predicate) {
    TurboAssembler::LoadZeroIfNotFPUCondition(dst);
  } else {
    TurboAssembler::LoadZeroIfFPUCondition(dst);
  }

  bind(&cont);
}

bool LiftoffAssembler::emit_select(LiftoffRegister dst, Register condition,
                                   LiftoffRegister true_value,
                                   LiftoffRegister false_value,
                                   ValueType type) {
  return false;
}

void LiftoffAssembler::LoadTransform(LiftoffRegister dst, Register src_addr,
                                     Register offset_reg, uint32_t offset_imm,
                                     LoadType type,
                                     LoadTransformationKind transform,
                                     uint32_t* protected_load_pc) {}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {}

void LiftoffAssembler::emit_i8x16_swizzle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  bailout(kSimd, "emit_s128_const");
}

void LiftoffAssembler::emit_s128_not(LiftoffRegister dst, LiftoffRegister src) {
}

void LiftoffAssembler::emit_s128_and(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_s128_or(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {}

void LiftoffAssembler::emit_s128_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_s128_and_not(LiftoffRegister dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {}

void LiftoffAssembler::emit_i8x16_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_v8x16_anytrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_v8x16_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {}

void LiftoffAssembler::emit_i8x16_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i8x16_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i8x16_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_add_saturate_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_add_saturate_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_sub_saturate_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_sub_saturate_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_v16x8_anytrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_v16x8_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {}

void LiftoffAssembler::emit_i16x8_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i16x8_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_add_saturate_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_add_saturate_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_sub_saturate_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_sub_saturate_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_v32x4_anytrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_v32x4_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {}

void LiftoffAssembler::emit_i32x4_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i32x4_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i64x2_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {}

void LiftoffAssembler::emit_i64x2_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i64x2_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {}

void LiftoffAssembler::emit_i64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {}

bool LiftoffAssembler::emit_f32x4_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {}

bool LiftoffAssembler::emit_f32x4_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {}

bool LiftoffAssembler::emit_f32x4_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f32x4_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_f64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_f64x2_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {}

bool LiftoffAssembler::emit_f64x2_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {}

bool LiftoffAssembler::emit_f64x2_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {}

bool LiftoffAssembler::emit_f64x2_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {}

void LiftoffAssembler::emit_f64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_f64x2_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i32x4_sconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_uconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {}

void LiftoffAssembler::emit_f32x4_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i16x8_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {}

void LiftoffAssembler::emit_i8x16_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {}

void LiftoffAssembler::StackCheck(Label* ool_code, Register limit_address) {
  TurboAssembler::Ld_d(limit_address, MemOperand(limit_address, 0));
  TurboAssembler::Branch(ool_code, ule, sp, Operand(limit_address));
}

void LiftoffAssembler::CallTrapCallbackForTesting() {
  PrepareCallCFunction(0, GetUnusedRegister(kGpReg, {}).gp());
  CallCFunction(ExternalReference::wasm_call_trap_callback_for_testing(), 0);
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  if (emit_debug_code()) Abort(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned num_gp_regs = gp_regs.GetNumRegsSet();
  if (num_gp_regs) {
    unsigned offset = num_gp_regs * kSystemPointerSize;
    addi_d(sp, sp, -offset);
    while (!gp_regs.is_empty()) {
      LiftoffRegister reg = gp_regs.GetFirstRegSet();
      offset -= kSystemPointerSize;
      St_d(reg.gp(), MemOperand(sp, offset));
      gp_regs.clear(reg);
    }
    DCHECK_EQ(offset, 0);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned num_fp_regs = fp_regs.GetNumRegsSet();
  if (num_fp_regs) {
    unsigned slot_size = /*IsEnabled(MIPS_SIMD) ? 16 :*/ 8;
    addi_d(sp, sp, -(num_fp_regs * slot_size));
    unsigned offset = 0;
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      if (0 /*IsEnabled(MIPS_SIMD)*/) {
        // TurboAssembler::st_d(reg.fp().toW(), MemOperand(sp, offset));
      } else {
        TurboAssembler::Fst_d(reg.fp(), MemOperand(sp, offset));
      }
      fp_regs.clear(reg);
      offset += slot_size;
    }
    DCHECK_EQ(offset, num_fp_regs * slot_size);
  }
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned fp_offset = 0;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetFirstRegSet();
    if (0 /*IsEnabled(MIPS_SIMD)*/) {
      // TurboAssembler::ld_d(reg.fp().toW(), MemOperand(sp, fp_offset));
    } else {
      TurboAssembler::Fld_d(reg.fp(), MemOperand(sp, fp_offset));
    }
    fp_regs.clear(reg);
    fp_offset += (/*IsEnabled(MIPS_SIMD) ? 16 :*/ 8);
  }
  if (fp_offset) addi_d(sp, sp, fp_offset);
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned gp_offset = 0;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetLastRegSet();
    Ld_d(reg.gp(), MemOperand(sp, gp_offset));
    gp_regs.clear(reg);
    gp_offset += kSystemPointerSize;
  }
  addi_d(sp, sp, gp_offset);
}

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  DCHECK_LT(num_stack_slots,
            (1 << 16) / kSystemPointerSize);  // 16 bit immediate
  TurboAssembler::DropAndRet(static_cast<int>(num_stack_slots));
}

void LiftoffAssembler::CallC(const wasm::FunctionSig* sig,
                             const LiftoffRegister* args,
                             const LiftoffRegister* rets,
                             ValueType out_argument_type, int stack_bytes,
                             ExternalReference ext_ref) {
  addi_d(sp, sp, -stack_bytes);

  int arg_bytes = 0;
  for (ValueType param_type : sig->parameters()) {
    liftoff::Store(this, sp, arg_bytes, *args++, param_type);
    arg_bytes += param_type.element_size_bytes();
  }
  DCHECK_LE(arg_bytes, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  // On mips, the first argument is passed in {a0}.
  constexpr Register kFirstArgReg = a0;
  mov(kFirstArgReg, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs, kScratchReg);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* next_result_reg = rets;
  if (sig->return_count() > 0) {
    DCHECK_EQ(1, sig->return_count());
    constexpr Register kReturnReg = a0;
    if (kReturnReg != next_result_reg->gp()) {
      Move(*next_result_reg, LiftoffRegister(kReturnReg), sig->GetReturn(0));
    }
    ++next_result_reg;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_type != kWasmStmt) {
    liftoff::Load(this, *next_result_reg, MemOperand(sp, 0), out_argument_type);
  }

  addi_d(sp, sp, stack_bytes);
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) {
  Call(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::TailCallNativeWasmCode(Address addr) {
  Jump(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::CallIndirect(const wasm::FunctionSig* sig,
                                    compiler::CallDescriptor* call_descriptor,
                                    Register target) {
  if (target == no_reg) {
    pop(kScratchReg);
    Call(kScratchReg);
  } else {
    Call(target);
  }
}

void LiftoffAssembler::TailCallIndirect(Register target) {
  if (target == no_reg) {
    Pop(kScratchReg);
    Jump(kScratchReg);
  } else {
    Jump(target);
  }
}

void LiftoffAssembler::CallRuntimeStub(WasmCode::RuntimeStubId sid) {
  // A direct call to a wasm runtime stub defined in this module.
  // Just encode the stub index. This will be patched at relocation.
  Call(static_cast<Address>(sid), RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  addi_d(sp, sp, -size);
  TurboAssembler::Move(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  addi_d(sp, sp, size);
}

void LiftoffStackSlots::Construct() {
  for (auto& slot : slots_) {
    const LiftoffAssembler::VarState& src = slot.src_;
    switch (src.loc()) {
      case LiftoffAssembler::VarState::kStack:
        if (src.type() != kWasmS128) {
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->push(kScratchReg);
        } else {
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_ - 8));
          asm_->push(kScratchReg);
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->push(kScratchReg);
        }
        break;
      case LiftoffAssembler::VarState::kRegister:
        liftoff::push(asm_, src.reg(), src.type());
        break;
      case LiftoffAssembler::VarState::kIntConst: {
        asm_->li(kScratchReg, Operand(src.i32_const()));
        asm_->push(kScratchReg);
        break;
      }
    }
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_H_
