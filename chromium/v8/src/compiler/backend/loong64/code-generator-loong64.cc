// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/heap/memory-chunk.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {
namespace compiler {

#define __ tasm()->

// TODO(plind): consider renaming these macros.
#define TRACE_MSG(msg)                                                      \
  PrintF("code_gen: \'%s\' in function %s at line %d\n", msg, __FUNCTION__, \
         __LINE__)

#define TRACE_UNIMPL()                                                       \
  PrintF("UNIMPLEMENTED code_generator_loong64: %s at line %d\n", __FUNCTION__, \
         __LINE__)

// Adds La64-specific methods to convert InstructionOperands.
class La64OperandConverter final : public InstructionOperandConverter {
 public:
  La64OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  FloatRegister OutputSingleRegister(size_t index = 0) {
    return ToSingleRegister(instr_->OutputAt(index));
  }

  FloatRegister InputSingleRegister(size_t index) {
    return ToSingleRegister(instr_->InputAt(index));
  }

  FloatRegister ToSingleRegister(InstructionOperand* op) {
    // Single (Float) and Double register namespace is same on LOONG64,
    // both are typedefs of FPURegister.
    return ToDoubleRegister(op);
  }

  Register InputOrZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, InputInt32(index));
      return zero_reg;
    }
    return InputRegister(index);
  }

  DoubleRegister InputOrZeroDoubleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kDoubleRegZero;

    return InputDoubleRegister(index);
  }

  DoubleRegister InputOrZeroSingleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kDoubleRegZero;

    return InputSingleRegister(index);
  }

  Operand InputImmediate(size_t index) {
    Constant constant = ToConstant(instr_->InputAt(index));
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32());
      case Constant::kInt64:
        return Operand(constant.ToInt64());
      case Constant::kFloat32:
        return Operand::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Operand::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kExternalReference:
      case Constant::kCompressedHeapObject:
      case Constant::kHeapObject:
        // TODO(plind): Maybe we should handle ExtRef & HeapObj here?
        //    maybe not done on arm due to const pool ??
        break;
      case Constant::kDelayedStringConstant:
        return Operand::EmbeddedStringConstant(
            constant.ToDelayedStringConstant());
      case Constant::kRpoNumber:
        UNREACHABLE();  // TODO(titzer): RPO immediates on loong64?
        break;
    }
    UNREACHABLE();
  }

  Operand InputOperand(size_t index) {
    InstructionOperand* op = instr_->InputAt(index);
    if (op->IsRegister()) {
      return Operand(ToRegister(op));
    }
    return InputImmediate(index);
  }

  MemOperand MemoryOperand(size_t* first_index) {
    const size_t index = *first_index;
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
        break;
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_MRR:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(size_t index = 0) { return MemoryOperand(&index); }

  MemOperand ToMemOperand(InstructionOperand* op) const {
    DCHECK_NOT_NULL(op);
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToMemOperand(AllocatedOperand::cast(op)->index());
  }

  MemOperand SlotToMemOperand(int slot) const {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }
};

static inline bool HasRegisterInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsRegister();
}

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(CodeGenerator* gen, Register object, Register index,
                       Register value, Register scratch0, Register scratch1,
                       RecordWriteMode mode, StubCallMode stub_mode)
      : OutOfLineCode(gen),
        object_(object),
        index_(index),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
        stub_mode_(stub_mode),
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        zone_(gen->zone()) {}

  void Generate() final {
    if (mode_ > RecordWriteMode::kValueIsPointer) {
      __ JumpIfSmi(value_, exit());
    }
    __ CheckPageFlag(value_, scratch0_,
                     MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());
    __ Add_d(scratch1_, object_, index_);
    RememberedSetAction const remembered_set_action =
        mode_ > RecordWriteMode::kValueIsMap ? EMIT_REMEMBERED_SET
                                             : OMIT_REMEMBERED_SET;
    SaveFPRegsMode const save_fp_mode =
        frame()->DidAllocateDoubleRegisters() ? kSaveFPRegs : kDontSaveFPRegs;
    if (must_save_lr_) {
      // We need to save and restore ra if the frame was elided.
      __ Push(ra);
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, scratch1_, save_fp_mode);
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ CallRecordWriteStub(object_, scratch1_, remembered_set_action,
                             save_fp_mode, wasm::WasmCode::kRecordWrite);
    } else {
      __ CallRecordWriteStub(object_, scratch1_, remembered_set_action,
                             save_fp_mode);
    }
    if (must_save_lr_) {
      __ Pop(ra);
    }
  }

 private:
  Register const object_;
  Register const index_;
  Register const value_;
  Register const scratch0_;
  Register const scratch1_;
  RecordWriteMode const mode_;
  StubCallMode const stub_mode_;
  bool must_save_lr_;
  Zone* zone_;
};

#define CREATE_OOL_CLASS(ool_name, tasm_ool_name, T)                 \
  class ool_name final : public OutOfLineCode {                      \
   public:                                                           \
    ool_name(CodeGenerator* gen, T dst, T src1, T src2)              \
        : OutOfLineCode(gen), dst_(dst), src1_(src1), src2_(src2) {} \
                                                                     \
    void Generate() final { __ tasm_ool_name(dst_, src1_, src2_); }  \
                                                                     \
   private:                                                          \
    T const dst_;                                                    \
    T const src1_;                                                   \
    T const src2_;                                                   \
  }

CREATE_OOL_CLASS(OutOfLineFloat32Max, Float32MaxOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat32Min, Float32MinOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat64Max, Float64MaxOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat64Min, Float64MinOutOfLine, FPURegister);

#undef CREATE_OOL_CLASS

Condition FlagsConditionToConditionCmp(FlagsCondition condition) {
  switch (condition) {
    case kEqual:
      return eq;
    case kNotEqual:
      return ne;
    case kSignedLessThan:
      return lt;
    case kSignedGreaterThanOrEqual:
      return ge;
    case kSignedLessThanOrEqual:
      return le;
    case kSignedGreaterThan:
      return gt;
    case kUnsignedLessThan:
      return lo;
    case kUnsignedGreaterThanOrEqual:
      return hs;
    case kUnsignedLessThanOrEqual:
      return ls;
    case kUnsignedGreaterThan:
      return hi;
    case kUnorderedEqual:
    case kUnorderedNotEqual:
      break;
    default:
      break;
  }
  UNREACHABLE();
}

Condition FlagsConditionToConditionTst(FlagsCondition condition) {
  switch (condition) {
    case kNotEqual:
      return ne;
    case kEqual:
      return eq;
    default:
      break;
  }
  UNREACHABLE();
}

Condition FlagsConditionToConditionOvf(FlagsCondition condition) {
  switch (condition) {
    case kOverflow:
      return ne;
    case kNotOverflow:
      return eq;
    default:
      break;
  }
  UNREACHABLE();
}

FPUCondition FlagsConditionToConditionCmpFPU(bool* predicate,
                                             FlagsCondition condition) {
  switch (condition) {
    case kEqual:
      *predicate = true;
      return CEQ;
    case kNotEqual:
      *predicate = false;
      return CEQ;
    case kUnsignedLessThan:
      *predicate = true;
      return CLT;
    case kUnsignedGreaterThanOrEqual:
      *predicate = false;
      return CLT;
    case kUnsignedLessThanOrEqual:
      *predicate = true;
      return CLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return CLE;
    case kUnorderedEqual:
    case kUnorderedNotEqual:
      *predicate = true;
      break;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
}

void EmitWordLoadPoisoningIfNeeded(CodeGenerator* codegen,
                                   InstructionCode opcode, Instruction* instr,
                                   La64OperandConverter const& i) {
  const MemoryAccessMode access_mode =
      static_cast<MemoryAccessMode>(MiscField::decode(opcode));
  if (access_mode == kMemoryAccessPoisoned) {
    Register value = i.OutputRegister();
    codegen->tasm()->And(value, value, kSpeculationPoisonRegister);
  }
}

}  // namespace

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr)          \
  do {                                                   \
    __ asm_instr(i.OutputRegister(), i.MemoryOperand()); \
    __ dbar(0);                                          \
  } while (0)

// TODO remove second dbar?
#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr)               \
  do {                                                         \
    __ dbar(0);                                                \
    __ asm_instr(i.InputOrZeroRegister(2), i.MemoryOperand()); \
    __ dbar(0);                                                \
  } while (0)

// only use for sub_w and sub_d
#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr)       \
  do {                                                                         \
    Label binop;                                                               \
    __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ dbar(0);                                                                \
    __ bind(&binop);                                                           \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ bin_instr(i.TempRegister(1), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ dbar(0);                                                                \
  } while (0)

// TODO remove second dbar?
#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, sign_extend, \
                                  size, bin_instr, representation)             \
  do {                                                                         \
    Label binop;                                                               \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(3)));                                      \
    __ slli_w(i.TempRegister(3), i.TempRegister(3), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&binop);                                                           \
    __ load_linked(i.TempRegister(1), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(1), i.TempRegister(3),  \
                   size, sign_extend);                                         \
    __ bin_instr(i.TempRegister(2), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ InsertBits(i.TempRegister(1), i.TempRegister(2), i.TempRegister(3),     \
                  size);                                                       \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ dbar(0);                                                                \
  } while (0)

// TODO remove second dbar?
#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(                                  \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label exchange;                                                            \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ slli_w(i.TempRegister(1), i.TempRegister(1), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&exchange);                                                        \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ InsertBits(i.TempRegister(2), i.InputRegister(2), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, eq, i.TempRegister(2), Operand(zero_reg));       \
    __ dbar(0);                                                                \
  } while (0)

// TODO remove second dbar?
#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,                  \
                                                 store_conditional)            \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ dbar(0);                                                                \
    __ bind(&compareExchange);                                                 \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ mov(i.TempRegister(2), i.InputRegister(3));                             \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, eq, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ dbar(0);                                                                \
  } while (0)

// TODO remove second dbar?
#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(                          \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ slli_w(i.TempRegister(1), i.TempRegister(1), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&compareExchange);                                                 \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ ExtractBits(i.InputRegister(2), i.InputRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ InsertBits(i.TempRegister(2), i.InputRegister(3), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, eq, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ dbar(0);                                                                \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                        \
  do {                                                                      \
    FrameScope scope(tasm(), StackFrame::MANUAL);                           \
    __ PrepareCallCFunction(0, 2, kScratchReg);                             \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                       \
                            i.InputDoubleRegister(1));                      \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2); \
    /* Move the result in the double result register. */                    \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                         \
  do {                                                                      \
    FrameScope scope(tasm(), StackFrame::MANUAL);                           \
    __ PrepareCallCFunction(0, 1, kScratchReg);                             \
    __ MovToFloatParameter(i.InputDoubleRegister(0));                       \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1); \
    /* Move the result in the double result register. */                    \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
  } while (0)

#define ASSEMBLE_F64X2_ARITHMETIC_BINOP(op)                     \
  do {                                                          \
    __ op(i.OutputSimd128Register(), i.InputSimd128Register(0), \
          i.InputSimd128Register(1));                           \
  } while (0)

void CodeGenerator::AssembleDeconstructFrame() {
  __ mov(sp, fp);
  __ Pop(ra, fp);
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ Ld_d(ra, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
    __ Ld_d(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  }
  frame_access_state()->SetFrameAccessToSP();
}

void CodeGenerator::AssemblePopArgumentsAdaptorFrame(Register args_reg,
                                                     Register scratch1,
                                                     Register scratch2,
                                                     Register scratch3) {
  DCHECK(!AreAliased(args_reg, scratch1, scratch2, scratch3));
  Label done;

  // Check if current frame is an arguments adaptor frame.
  __ Ld_d(scratch3, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ Branch(&done, ne, scratch3,
            Operand(StackFrame::TypeToMarker(StackFrame::ARGUMENTS_ADAPTOR)));

  // Load arguments count from current arguments adaptor frame (note, it
  // does not include receiver).
  Register caller_args_count_reg = scratch1;
  __ Ld_d(caller_args_count_reg,
          MemOperand(fp, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ SmiUntag(caller_args_count_reg);

  __ PrepareForTailCall(args_reg, caller_args_count_reg, scratch2, scratch3);
  __ bind(&done);
}

namespace {

void AdjustStackPointerForTailCall(TurboAssembler* tasm,
                                   FrameAccessState* state,
                                   int new_slot_above_sp,
                                   bool allow_shrinkage = true) {
  int current_sp_offset = state->GetSPToFPSlotCount() +
                          StandardFrameConstants::kFixedSlotCountAboveFp;
  int stack_slot_delta = new_slot_above_sp - current_sp_offset;
  if (stack_slot_delta > 0) {
    tasm->Sub_d(sp, sp, stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    tasm->Add_d(sp, sp, -stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_stack_slot) {
  AdjustStackPointerForTailCall(tasm(), frame_access_state(),
                                first_unused_stack_slot, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_stack_slot) {
  AdjustStackPointerForTailCall(tasm(), frame_access_state(),
                                first_unused_stack_slot);
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  __ ComputeCodeStartAddress(kScratchReg);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart,
            kJavaScriptCallCodeStartRegister, Operand(kScratchReg));
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {CodeDataContainer} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void CodeGenerator::BailoutIfDeoptimized() {
  int offset = Code::kCodeDataContainerOffset - Code::kHeaderSize;
  __ Ld_d(kScratchReg, MemOperand(kJavaScriptCallCodeStartRegister, offset));
  __ Ld_w(kScratchReg,
          FieldMemOperand(kScratchReg,
                          CodeDataContainer::kKindSpecificFlagsOffset));
  __ And(kScratchReg, kScratchReg,
         Operand(1 << Code::kMarkedForDeoptimizationBit));
  __ Jump(BUILTIN_CODE(isolate(), CompileLazyDeoptimizedCode),
          RelocInfo::CODE_TARGET, ne, kScratchReg, Operand(zero_reg));
}

void CodeGenerator::GenerateSpeculationPoisonFromCodeStartRegister() {
  // Calculate a mask which has all bits set in the normal case, but has all
  // bits cleared if we are speculatively executing the wrong PC.
  __ li(kSpeculationPoisonRegister, -1);
  __ ComputeCodeStartAddress(kScratchReg);
  __ sub_d(kScratchReg, kScratchReg, kJavaScriptCallCodeStartRegister);
  __ maskeqz(kSpeculationPoisonRegister, kSpeculationPoisonRegister,
             kScratchReg);
}

void CodeGenerator::AssembleRegisterArgumentPoisoning() {
  __ And(kJSFunctionRegister, kJSFunctionRegister, kSpeculationPoisonRegister);
  __ And(kContextRegister, kContextRegister, kSpeculationPoisonRegister);
  __ And(sp, sp, kSpeculationPoisonRegister);
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  La64OperandConverter i(this, instr);
  InstructionCode opcode = instr->opcode();
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);
  switch (arch_opcode) {
    case kArchCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Call(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            HasCallDescriptorFlag(instr, CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ addi_d(reg, reg, Code::kHeaderSize - kHeapObjectTag);
        __ Call(reg);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchCallBuiltinPointer: {
      DCHECK(!instr->InputAt(0)->IsImmediate());
      Register builtin_index = i.InputRegister(0);
      __ CallBuiltinByIndex(builtin_index);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchCallWasmFunction: {
      if (arch_opcode == kArchTailCallCodeObjectFromJSFunction) {
        AssemblePopArgumentsAdaptorFrame(kJavaScriptCallArgCountRegister,
                                         i.TempRegister(0), i.TempRegister(1),
                                         i.TempRegister(2));
      }
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Call(wasm_code, constant.rmode());
      } else {
        __ addi_d(kScratchReg, i.InputRegister(0), 0);
        __ Call(kScratchReg);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallCodeObjectFromJSFunction:
    case kArchTailCallCodeObject: {
      if (arch_opcode == kArchTailCallCodeObjectFromJSFunction) {
        AssemblePopArgumentsAdaptorFrame(kJavaScriptCallArgCountRegister,
                                         i.TempRegister(0), i.TempRegister(1),
                                         i.TempRegister(2));
      }
      if (instr->InputAt(0)->IsImmediate()) {
        __ Jump(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            HasCallDescriptorFlag(instr, CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ addi_d(reg, reg, Code::kHeaderSize - kHeapObjectTag);
        __ Jump(reg);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallWasm: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Jump(wasm_code, constant.rmode());
      } else {
        __ addi_d(kScratchReg, i.InputRegister(0), 0);
        __ Jump(kScratchReg);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallAddress: {
      CHECK(!instr->InputAt(0)->IsImmediate());
      Register reg = i.InputRegister(0);
      DCHECK_IMPLIES(
          HasCallDescriptorFlag(instr, CallDescriptor::kFixedTargetRegister),
          reg == kJavaScriptCallCodeStartRegister);
      __ Jump(reg);
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputRegister(0);
      if (FLAG_debug_code) {
        // Check the function's context matches the context argument.
        __ Ld_d(kScratchReg, FieldMemOperand(func, JSFunction::kContextOffset));
        __ Assert(eq, AbortReason::kWrongFunctionContext, cp,
                  Operand(kScratchReg));
      }
      static_assert(kJavaScriptCallCodeStartRegister == a2, "ABI mismatch");
      __ Ld_d(a2, FieldMemOperand(func, JSFunction::kCodeOffset));
      __ Add_d(a2, a2, Operand(Code::kHeaderSize - kHeapObjectTag));
      __ Call(a2);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      int const num_parameters = MiscField::decode(instr->opcode());
      __ PrepareCallCFunction(num_parameters, kScratchReg);
      // Frame alignment requires using FP-relative frame addressing.
      frame_access_state()->SetFrameAccessToFP();
      break;
    }
    case kArchSaveCallerRegisters: {
      fp_mode_ =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      DCHECK(fp_mode_ == kDontSaveFPRegs || fp_mode_ == kSaveFPRegs);
      // kReturnRegister0 should have been saved before entering the stub.
      int bytes = __ PushCallerSaved(fp_mode_, kReturnRegister0);
      DCHECK(IsAligned(bytes, kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      DCHECK(!caller_registers_saved_);
      caller_registers_saved_ = true;
      break;
    }
    case kArchRestoreCallerRegisters: {
      DCHECK(fp_mode_ ==
             static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode())));
      DCHECK(fp_mode_ == kDontSaveFPRegs || fp_mode_ == kSaveFPRegs);
      // Don't overwrite the returned value.
      int bytes = __ PopCallerSaved(fp_mode_, kReturnRegister0);
      frame_access_state()->IncreaseSPDelta(-(bytes / kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      DCHECK(caller_registers_saved_);
      caller_registers_saved_ = false;
      break;
    }
    case kArchPrepareTailCall:
      AssemblePrepareTailCall();
      break;
    case kArchCallCFunction: {
      int const num_parameters = MiscField::decode(instr->opcode());
      Label start_call;
      bool isWasmCapiFunction =
          linkage()->GetIncomingDescriptor()->IsWasmCapiFunction();
      // from start_call to return address.
      int offset = __ root_array_available() ? 44 : 80;  // 11 or 20 instrs
#if V8_HOST_ARCH_LOONG64
      if (__ emit_debug_code()) {
        offset += 12;  // see CallCFunction
      }
#endif
      if (isWasmCapiFunction) {
        // Put the return address in a stack slot.
        // __ mov(kScratchReg, ra);
        __ bind(&start_call);
        __ pcaddi(t7, -4);  // __ nal();
        //__ nop();
        //__ Daddu(ra, ra, offset - 8);  // 8 = nop + nal
        __ St_d(t7, MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
        // __ mov(ra, kScratchReg);
      }
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        __ CallCFunction(ref, num_parameters);
      } else {
        Register func = i.InputRegister(0);
        __ CallCFunction(func, num_parameters);
      }
      if (isWasmCapiFunction) {
        CHECK_EQ(offset, __ SizeOfCodeGeneratedSince(&start_call));
        RecordSafepoint(instr->reference_map(), Safepoint::kNoLazyDeopt);
      }

      frame_access_state()->SetFrameAccessToDefault();
      // Ideally, we should decrement SP delta to match the change of stack
      // pointer in CallCFunction. However, for certain architectures (e.g.
      // ARM), there may be more strict alignment requirement, causing old SP
      // to be saved on the stack. In those cases, we can not calculate the SP
      // delta statically.
      frame_access_state()->ClearSPDelta();
      if (caller_registers_saved_) {
        // Need to re-sync SP delta introduced in kArchSaveCallerRegisters.
        // Here, we assume the sequence to be:
        //   kArchSaveCallerRegisters;
        //   kArchCallCFunction;
        //   kArchRestoreCallerRegisters;
        int bytes =
            __ RequiredStackSizeForCallerSaved(fp_mode_, kReturnRegister0);
        frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      }
      break;
    }
    case kArchJmp:
      AssembleArchJump(i.InputRpo(0));
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      break;
    case kArchAbortCSAAssert:
      DCHECK(i.InputRegister(0) == a0);
      {
        // We don't actually want to generate a pile of code for this, so just
        // claim there is a stack frame, without generating one.
        FrameScope scope(tasm(), StackFrame::NONE);
        __ Call(
            isolate()->builtins()->builtin_handle(Builtins::kAbortCSAAssert),
            RelocInfo::CODE_TARGET);
      }
      __ stop();
      break;
    case kArchDebugBreak:
      __ DebugBreak();
      break;
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt64(0)));
      break;
    case kArchNop:
    case kArchThrowTerminator:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, OutputFrameStateCombine::Ignore());
      CodeGenResult result = AssembleDeoptimizerCall(exit);
      if (result != kSuccess) return result;
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      break;
    case kArchStackPointerGreaterThan:
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), fp);
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ Ld_d(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mov(i.OutputRegister(), fp);
      }
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      break;
    case kArchStoreWithWriteBarrier: {
      RecordWriteMode mode =
          static_cast<RecordWriteMode>(MiscField::decode(instr->opcode()));
      Register object = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);
      auto ool = zone()->New<OutOfLineRecordWrite>(this, object, index, value,
                                                   scratch0, scratch1, mode,
                                                   DetermineStubCallMode());
      __ Add_d(kScratchReg, object, index);
      __ St_d(value, MemOperand(kScratchReg, 0));
      __ CheckPageFlag(object, scratch0,
                       MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                       ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base_reg = offset.from_stack_pointer() ? sp : fp;
      __ Add_d(i.OutputRegister(), base_reg, Operand(offset.offset()));
      int alignment = i.InputInt32(1);
      DCHECK(alignment == 0 || alignment == 4 || alignment == 8 ||
             alignment == 16);
      if (FLAG_debug_code && alignment > 0) {
        // Verify that the output_register is properly aligned
        __ And(kScratchReg, i.OutputRegister(),
               Operand(kSystemPointerSize - 1));
        __ Assert(eq, AbortReason::kAllocationIsNotDoubleAligned, kScratchReg,
                  Operand(zero_reg));
      }
      if (alignment == 2 * kSystemPointerSize) {
        Label done;
        __ Add_d(kScratchReg, base_reg, Operand(offset.offset()));
        __ And(kScratchReg, kScratchReg, Operand(alignment - 1));
        __ BranchShort(&done, eq, kScratchReg, Operand(zero_reg));
        __ Add_d(i.OutputRegister(), i.OutputRegister(), kSystemPointerSize);
        __ bind(&done);
      } else if (alignment > 2 * kSystemPointerSize) {
        Label done;
        __ Add_d(kScratchReg, base_reg, Operand(offset.offset()));
        __ And(kScratchReg, kScratchReg, Operand(alignment - 1));
        __ BranchShort(&done, eq, kScratchReg, Operand(zero_reg));
        __ li(kScratchReg2, alignment);
        __ Sub_d(kScratchReg2, kScratchReg2, Operand(kScratchReg));
        __ Add_d(i.OutputRegister(), i.OutputRegister(), kScratchReg2);
        __ bind(&done);
      }

      break;
    }
    case kArchWordPoisonOnSpeculation:
      __ And(i.OutputRegister(), i.InputRegister(0),
             kSpeculationPoisonRegister);
      break;
    case kIeee754Float64Acos:
      ASSEMBLE_IEEE754_UNOP(acos);
      break;
    case kIeee754Float64Acosh:
      ASSEMBLE_IEEE754_UNOP(acosh);
      break;
    case kIeee754Float64Asin:
      ASSEMBLE_IEEE754_UNOP(asin);
      break;
    case kIeee754Float64Asinh:
      ASSEMBLE_IEEE754_UNOP(asinh);
      break;
    case kIeee754Float64Atan:
      ASSEMBLE_IEEE754_UNOP(atan);
      break;
    case kIeee754Float64Atanh:
      ASSEMBLE_IEEE754_UNOP(atanh);
      break;
    case kIeee754Float64Atan2:
      ASSEMBLE_IEEE754_BINOP(atan2);
      break;
    case kIeee754Float64Cos:
      ASSEMBLE_IEEE754_UNOP(cos);
      break;
    case kIeee754Float64Cosh:
      ASSEMBLE_IEEE754_UNOP(cosh);
      break;
    case kIeee754Float64Cbrt:
      ASSEMBLE_IEEE754_UNOP(cbrt);
      break;
    case kIeee754Float64Exp:
      ASSEMBLE_IEEE754_UNOP(exp);
      break;
    case kIeee754Float64Expm1:
      ASSEMBLE_IEEE754_UNOP(expm1);
      break;
    case kIeee754Float64Log:
      ASSEMBLE_IEEE754_UNOP(log);
      break;
    case kIeee754Float64Log1p:
      ASSEMBLE_IEEE754_UNOP(log1p);
      break;
    case kIeee754Float64Log2:
      ASSEMBLE_IEEE754_UNOP(log2);
      break;
    case kIeee754Float64Log10:
      ASSEMBLE_IEEE754_UNOP(log10);
      break;
    case kIeee754Float64Pow:
      ASSEMBLE_IEEE754_BINOP(pow);
      break;
    case kIeee754Float64Sin:
      ASSEMBLE_IEEE754_UNOP(sin);
      break;
    case kIeee754Float64Sinh:
      ASSEMBLE_IEEE754_UNOP(sinh);
      break;
    case kIeee754Float64Tan:
      ASSEMBLE_IEEE754_UNOP(tan);
      break;
    case kIeee754Float64Tanh:
      ASSEMBLE_IEEE754_UNOP(tanh);
      break;
    case kLa64Add:
      __ Add_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Dadd:
      __ Add_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64DaddOvf:
      __ AdddOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                      kScratchReg);
      break;
    case kLa64Sub:
      __ Sub_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Dsub:
      __ Sub_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64DsubOvf:
      __ SubdOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                      kScratchReg);
      break;
    case kLa64Mul:
      __ Mul_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64MulOvf:
      __ MulOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                     kScratchReg);
      break;
    case kLa64MulHigh:
      __ Mulh_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64MulHighU:
      __ Mulh_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64DMulHigh:
      __ Mulh_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Div:
      __ Div_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ masknez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLa64DivU:
      __ Div_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ masknez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLa64Mod:
      __ Mod_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64ModU:
      __ Mod_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Dmul:
      __ Mul_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Ddiv:
      __ Div_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ masknez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLa64DdivU:
      __ Div_du(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ masknez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLa64Dmod:
      __ Mod_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64DmodU:
      __ Mod_du(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Dlsa:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Alsl_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.InputInt8(2), t7);
      break;
    case kLa64Lsa:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Alsl_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.InputInt8(2), t7);
      break;
    case kLa64And:
      __ And(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64And32:
      __ And(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ slli_w(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kLa64Or:
      __ Or(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Or32:
      __ Or(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ slli_w(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kLa64Nor:
      if (instr->InputAt(1)->IsRegister()) {
        __ Nor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      } else {
        DCHECK_EQ(0, i.InputOperand(1).immediate());
        __ Nor(i.OutputRegister(), i.InputRegister(0), zero_reg);
      }
      break;
    case kLa64Nor32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Nor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
        __ slli_w(i.OutputRegister(), i.OutputRegister(), 0x0);
      } else {
        DCHECK_EQ(0, i.InputOperand(1).immediate());
        __ Nor(i.OutputRegister(), i.InputRegister(0), zero_reg);
        __ slli_w(i.OutputRegister(), i.OutputRegister(), 0x0);
      }
      break;
    case kLa64Xor:
      __ Xor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Xor32:
      __ Xor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ slli_w(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kLa64Clz:
      __ Clz_w(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLa64Dclz:
      __ clz_d(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLa64Ctz: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Ctz_w(dst, src);
    } break;
    case kLa64Dctz: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Ctz_d(dst, src);
    } break;
    case kLa64Popcnt: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Popcnt_w(dst, src);
    } break;
    case kLa64Dpopcnt: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Popcnt_d(dst, src);
    } break;
    case kLa64Shl:
      if (instr->InputAt(1)->IsRegister()) {
        __ sll_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_w(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLa64Shr:
      if (instr->InputAt(1)->IsRegister()) {
        __ slli_w(i.InputRegister(0), i.InputRegister(0), 0x0);
        __ srl_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_w(i.OutputRegister(), i.InputRegister(0), 0x0);
        __ srli_w(i.OutputRegister(), i.OutputRegister(),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLa64Sar:
      if (instr->InputAt(1)->IsRegister()) {
        __ slli_w(i.InputRegister(0), i.InputRegister(0), 0x0);
        __ sra_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_w(i.OutputRegister(), i.InputRegister(0), 0x0);
        __ srai_w(i.OutputRegister(), i.OutputRegister(),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLa64Ext:
      __ bstrpick_w(i.OutputRegister(), i.InputRegister(0),
                    i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      break;
    case kLa64Ins:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ bstrins_w(i.OutputRegister(), zero_reg,
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      } else {
        __ bstrins_w(i.OutputRegister(), i.InputRegister(0),
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      }
      break;
    case kLa64Dext: {
      __ bstrpick_d(i.OutputRegister(), i.InputRegister(0),
                    i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      break;
    }
    case kLa64Dins:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ bstrins_d(i.OutputRegister(), zero_reg,
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      } else {
        __ bstrins_d(i.OutputRegister(), i.InputRegister(0),
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      }
      break;
    case kLa64Dshl:
      if (instr->InputAt(1)->IsRegister()) {
        __ sll_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_d(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLa64Dshr:
      if (instr->InputAt(1)->IsRegister()) {
        __ srl_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srli_d(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLa64Dsar:
      if (instr->InputAt(1)->IsRegister()) {
        __ sra_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srai_d(i.OutputRegister(), i.InputRegister(0), imm);
      }
      break;
    case kLa64Ror:
      __ Rotr_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Dror:
      __ Rotr_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLa64Tst:
      __ And(kScratchReg, i.InputRegister(0), i.InputOperand(1));
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kLa64Cmp:
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kLa64Mov:
      // TODO(plind): Should we combine mov/li like this, or use separate instr?
      //    - Also see x64 ASSEMBLE_BINOP & RegisterOrOperandType
      if (HasRegisterInput(instr, 0)) {
        __ mov(i.OutputRegister(), i.InputRegister(0));
      } else {
        __ li(i.OutputRegister(), i.InputOperand(0));
      }
      break;

    case kLa64CmpS: {
      FPURegister left = i.InputOrZeroSingleRegister(0);
      FPURegister right = i.InputOrZeroSingleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());

      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ CompareF32(left, right, cc);
    } break;
    case kLa64AddS:
      // TODO(plind): add special case: combine mult & add.
      __ fadd_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64SubS:
      __ fsub_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64MulS:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ fmul_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64DivS:
      __ fdiv_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64ModS: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(tasm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2, kScratchReg);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      // TODO(balazs.kilvady): implement mod_two_floats_operation(isolate())
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputSingleRegister());
      break;
    }
    case kLa64AbsS:
      __ fabs_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kLa64NegS:
      __ Neg_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kLa64SqrtS: {
      __ fsqrt_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64MaxS:
      __ fmax_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64MinS:
      __ fmin_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64CmpD: {
      FPURegister left = i.InputOrZeroDoubleRegister(0);
      FPURegister right = i.InputOrZeroDoubleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());
      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ CompareF64(left, right, cc);
    } break;
    case kLa64AddD:
      // TODO(plind): add special case: combine mult & add.
      __ fadd_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64SubD:
      __ fsub_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64MulD:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ fmul_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64DivD:
      __ fdiv_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64ModD: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(tasm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2, kScratchReg);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputDoubleRegister());
      break;
    }
    case kLa64AbsD:
      __ fabs_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64NegD:
      __ Neg_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64SqrtD: {
      __ fsqrt_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64MaxD:
      __ fmax_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64MinD:
      __ fmin_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLa64Float64RoundDown: {
      __ Floor_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64Float32RoundDown: {
      __ Floor_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLa64Float64RoundTruncate: {
      __ Trunc_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64Float32RoundTruncate: {
      __ Trunc_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLa64Float64RoundUp: {
      __ Ceil_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64Float32RoundUp: {
      __ Ceil_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLa64Float64RoundTiesEven: {
      __ Round_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLa64Float32RoundTiesEven: {
      __ Round_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLa64Float32Max: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Max>(this, dst, src1, src2);
      __ Float32Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLa64Float64Max: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Max>(this, dst, src1, src2);
      __ Float64Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLa64Float32Min: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Min>(this, dst, src1, src2);
      __ Float32Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLa64Float64Min: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Min>(this, dst, src1, src2);
      __ Float64Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLa64Float64SilenceNaN:
      __ FPUCanonicalizeNaN(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64CvtSD:
      __ fcvt_s_d(i.OutputSingleRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64CvtDS:
      __ fcvt_d_s(i.OutputDoubleRegister(), i.InputSingleRegister(0));
      break;
    case kLa64CvtDW: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_w(scratch, i.InputRegister(0));
      __ ffint_d_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLa64CvtSW: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_w(scratch, i.InputRegister(0));
      __ ffint_s_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLa64CvtSUw: {
      __ Ffint_s_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLa64CvtSL: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_d(scratch, i.InputRegister(0));
      __ ffint_s_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLa64CvtDL: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_d(scratch, i.InputRegister(0));
      __ ffint_d_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLa64CvtDUw: {
      __ Ffint_d_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLa64CvtDUl: {
      __ Ffint_d_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLa64CvtSUl: {
      __ Ffint_s_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLa64FloorWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrm_w_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64CeilWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrp_w_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64RoundWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrne_w_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64TruncWD: {
      FPURegister scratch = kScratchDoubleReg;
      // Other arches use round to zero here, so we follow.
      __ ftintrz_w_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64FloorWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrm_w_s(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64CeilWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrp_w_s(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64RoundWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrne_w_s(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      break;
    }
    case kLa64TruncWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrz_w_s(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
      // because INT32_MIN allows easier out-of-bounds detection.
      __ addi_w(kScratchReg, i.OutputRegister(), 1);
      __ slt(kScratchReg2, kScratchReg, i.OutputRegister());
      __ Movn(i.OutputRegister(), kScratchReg, kScratchReg2);
      break;
    }
    case kLa64TruncLS: {
      FPURegister scratch = kScratchDoubleReg;
      Register tmp_fcsr = kScratchReg;
      Register result = kScratchReg2;

      bool load_status = instr->OutputCount() > 1;
      if (load_status) {
        // Save FCSR.
        __ movfcsr2gr(tmp_fcsr);  // __ cfc1(tmp_fcsr, FCSR);
        // Clear FPU flags.
        __ movgr2fcsr(zero_reg);  // __ ctc1(zero_reg, FCSR);
      }
      // Other arches use round to zero here, so we follow.
      __ ftintrz_l_s(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_d(i.OutputRegister(), scratch);
      if (load_status) {
        __ movfcsr2gr(result);  // __ cfc1(result, FCSR);
        // Check for overflow and NaNs.
        __ And(result, result,
               (kFCSROverflowFlagMask | kFCSRInvalidOpFlagMask));
        __ Slt(result, zero_reg, result);
        __ xori(result, result, 1);
        __ mov(i.OutputRegister(1), result);
        // Restore FCSR
        __ movgr2fcsr(tmp_fcsr);  // __ ctc1(tmp_fcsr, FCSR);
      }
      break;
    }
    case kLa64TruncLD: {
      FPURegister scratch = kScratchDoubleReg;
      Register tmp_fcsr = kScratchReg;
      Register result = kScratchReg2;

      bool load_status = instr->OutputCount() > 1;
      if (load_status) {
        // Save FCSR.
        __ movfcsr2gr(tmp_fcsr);  // __ cfc1(tmp_fcsr, FCSR);
        // Clear FPU flags.
        __ movgr2fcsr(zero_reg);  // __ ctc1(zero_reg, FCSR);
      }
      // Other arches use round to zero here, so we follow.
      __ ftintrz_l_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_d(i.OutputRegister(0), scratch);
      if (load_status) {
        __ movfcsr2gr(result);  // __ cfc1(result, FCSR);
        // Check for overflow and NaNs.
        __ And(result, result,
               (kFCSROverflowFlagMask | kFCSRInvalidOpFlagMask));
        __ Slt(result, zero_reg, result);
        __ xori(result, result, 1);
        __ mov(i.OutputRegister(1), result);
        // Restore FCSR
        __ movgr2fcsr(tmp_fcsr);  // __ ctc1(tmp_fcsr, FCSR);
      }
      break;
    }
    case kLa64TruncUwD: {
      FPURegister scratch = kScratchDoubleReg;
      __ Ftintrz_uw_d(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      break;
    }
    case kLa64TruncUwS: {
      FPURegister scratch = kScratchDoubleReg;
      __ Ftintrz_uw_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
      // because 0 allows easier out-of-bounds detection.
      __ addi_w(kScratchReg, i.OutputRegister(), 1);
      __ Movz(i.OutputRegister(), zero_reg, kScratchReg);
      break;
    }
    case kLa64TruncUlS: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ftintrz_ul_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch,
                      result);
      break;
    }
    case kLa64TruncUlD: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ftintrz_ul_d(i.OutputRegister(0), i.InputDoubleRegister(0), scratch,
                      result);
      break;
    }
    case kLa64BitcastDL:
      __ movfr2gr_d(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64BitcastLD:
      __ movgr2fr_d(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kLa64Float64ExtractLowWord32:
      __ FmoveLow(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64Float64ExtractHighWord32:
      __ movfrh2gr_s(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLa64Float64InsertLowWord32:
      __ FmoveLow(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
    case kLa64Float64InsertHighWord32:
      __ movgr2frh_w(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
      // ... more basic instructions ...

    case kLa64Seb:
      __ ext_w_b(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLa64Seh:
      __ ext_w_h(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLa64Lbu:
      __ Ld_bu(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Lb:
      __ Ld_b(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Sb:
      __ St_b(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Lhu:
      __ Ld_hu(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Ulhu:
      __ Ld_hu(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Lh:
      __ Ld_h(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Ulh:
      __ Ld_h(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Sh:
      __ St_h(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Ush:
      __ St_h(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Lw:
      __ Ld_w(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Ulw:
      __ Ld_w(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Lwu:
      __ Ld_wu(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Ulwu:
      __ Ld_wu(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Ld:
      __ Ld_d(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Uld:
      __ Ld_d(i.OutputRegister(), i.MemoryOperand());
      EmitWordLoadPoisoningIfNeeded(this, opcode, instr, i);
      break;
    case kLa64Sw:
      __ St_w(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Usw:
      __ St_w(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Sd:
      __ St_d(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Usd:
      __ St_d(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kLa64Lwc1: {
      __ Fld_s(i.OutputSingleRegister(), i.MemoryOperand());
      break;
    }
    case kLa64Ulwc1: {
      __ Fld_s(i.OutputSingleRegister(), i.MemoryOperand());
      break;
    }
    case kLa64Swc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ Fst_s(ft, operand);
      break;
    }
    case kLa64Uswc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ Fst_s(ft, operand);
      break;
    }
    case kLa64Ldc1:
      __ Fld_d(i.OutputDoubleRegister(), i.MemoryOperand());
      break;
    case kLa64Uldc1:
      __ Fld_d(i.OutputDoubleRegister(), i.MemoryOperand());
      break;
    case kLa64Sdc1: {
      FPURegister ft = i.InputOrZeroDoubleRegister(2);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ Fst_d(ft, i.MemoryOperand());
      break;
    }
    case kLa64Usdc1: {
      FPURegister ft = i.InputOrZeroDoubleRegister(2);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ Fst_d(ft, i.MemoryOperand());
      break;
    }
    case kLa64Sync: {
      __ dbar(0);
      break;
    }
    case kLa64Push:
      if (instr->InputAt(0)->IsFPRegister()) {
        __ Fst_d(i.InputDoubleRegister(0), MemOperand(sp, -kDoubleSize));
        __ Sub_d(sp, sp, Operand(kDoubleSize));
        frame_access_state()->IncreaseSPDelta(kDoubleSize / kSystemPointerSize);
      } else {
        __ Push(i.InputRegister(0));
        frame_access_state()->IncreaseSPDelta(1);
      }
      break;
    case kLa64Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ Fld_d(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ Fld_s(
              i.OutputSingleRegister(0),
              MemOperand(fp, offset + kLessSignificantWordInDoublewordOffset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          abort();
        }
      } else {
        __ Ld_d(i.OutputRegister(0), MemOperand(fp, offset));
      }
      break;
    }
    case kLa64StackClaim: {
      __ Sub_d(sp, sp, Operand(i.InputInt32(0)));
      frame_access_state()->IncreaseSPDelta(i.InputInt32(0) /
                                            kSystemPointerSize);
      break;
    }
    case kLa64StoreToStackSlot: {
      if (instr->InputAt(0)->IsFPRegister()) {
        __ Fst_d(i.InputDoubleRegister(0), MemOperand(sp, i.InputInt32(1)));
      } else {
        __ St_d(i.InputRegister(0), MemOperand(sp, i.InputInt32(1)));
      }
      break;
    }
    case kLa64ByteSwap64: {
      __ ByteSwapSigned(i.OutputRegister(0), i.InputRegister(0), 8);
      break;
    }
    case kLa64ByteSwap32: {
      __ ByteSwapSigned(i.OutputRegister(0), i.InputRegister(0), 4);
      break;
    }
    case kWord32AtomicLoadInt8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_b);
      break;
    case kWord32AtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_bu);
      break;
    case kWord32AtomicLoadInt16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_h);
      break;
    case kWord32AtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_hu);
      break;
    case kWord32AtomicLoadWord32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_w);
      break;
    case kLa64Word64AtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_bu);
      break;
    case kLa64Word64AtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_hu);
      break;
    case kLa64Word64AtomicLoadUint32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_wu);
      break;
    case kLa64Word64AtomicLoadUint64:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_d);
      break;
    case kWord32AtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_b);
      break;
    case kWord32AtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_h);
      break;
    case kWord32AtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_w);
      break;
    case kLa64Word64AtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_b);
      break;
    case kLa64Word64AtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_h);
      break;
    case kLa64Word64AtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_w);
      break;
    case kLa64Word64AtomicStoreWord64:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_d);
      break;
    case kWord32AtomicExchangeInt8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 8, 32);
      break;
    case kWord32AtomicExchangeUint8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 8, 32);
      break;
    case kWord32AtomicExchangeInt16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 16, 32);
      break;
    case kWord32AtomicExchangeUint16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 16, 32);
      break;
    case kWord32AtomicExchangeWord32:
      __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amswap_db_w(i.OutputRegister(0), i.InputRegister(2),
                     i.TempRegister(0));
      break;
    case kLa64Word64AtomicExchangeUint8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 8, 64);
      break;
    case kLa64Word64AtomicExchangeUint16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 16, 64);
      break;
    case kLa64Word64AtomicExchangeUint32:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 32, 64);
      break;
    case kLa64Word64AtomicExchangeUint64:
      __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amswap_db_d(i.OutputRegister(0), i.InputRegister(2),
                     i.TempRegister(0));
      break;
    case kWord32AtomicCompareExchangeInt8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 8, 32);
      break;
    case kWord32AtomicCompareExchangeUint8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 8, 32);
      break;
    case kWord32AtomicCompareExchangeInt16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 16, 32);
      break;
    case kWord32AtomicCompareExchangeUint16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 16, 32);
      break;
    case kWord32AtomicCompareExchangeWord32:
      __ slli_w(i.InputRegister(2), i.InputRegister(2), 0);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_w, Sc_w);
      break;
    case kLa64Word64AtomicCompareExchangeUint8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 8, 64);
      break;
    case kLa64Word64AtomicCompareExchangeUint16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 16, 64);
      break;
    case kLa64Word64AtomicCompareExchangeUint32:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 32, 64);
      break;
    case kLa64Word64AtomicCompareExchangeUint64:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_d, Sc_d);
      break;
    case kWord32AtomicAddWord32:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amadd_db_w(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kWord32AtomicSubWord32:
      ASSEMBLE_ATOMIC_BINOP(Ll_w, Sc_w, Sub_w);
      break;
    case kWord32AtomicAndWord32:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amand_db_w(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kWord32AtomicOrWord32:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amor_db_w(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kWord32AtomicXorWord32:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amxor_db_w(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
#define ATOMIC_BINOP_CASE(op, inst)                             \
  case kWord32Atomic##op##Int8:                                 \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, true, 8, inst, 32);   \
    break;                                                      \
  case kWord32Atomic##op##Uint8:                                \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, false, 8, inst, 32);  \
    break;                                                      \
  case kWord32Atomic##op##Int16:                                \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, true, 16, inst, 32);  \
    break;                                                      \
  case kWord32Atomic##op##Uint16:                               \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, false, 16, inst, 32); \
    break;
      ATOMIC_BINOP_CASE(Add, Add_w)
      ATOMIC_BINOP_CASE(Sub, Sub_w)
      ATOMIC_BINOP_CASE(And, And)
      ATOMIC_BINOP_CASE(Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor)
#undef ATOMIC_BINOP_CASE

    case kLa64Word64AtomicAddUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amadd_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLa64Word64AtomicSubUint64:
      ASSEMBLE_ATOMIC_BINOP(Ll_d, Sc_d, Sub_d);
      break;
    case kLa64Word64AtomicAndUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amand_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLa64Word64AtomicOrUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amor_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLa64Word64AtomicXorUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      __ amxor_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
#define ATOMIC_BINOP_CASE(op, inst)                             \
  case kLa64Word64Atomic##op##Uint8:                            \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 8, inst, 64);  \
    break;                                                      \
  case kLa64Word64Atomic##op##Uint16:                           \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 16, inst, 64); \
    break;                                                      \
  case kLa64Word64Atomic##op##Uint32:                           \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, inst, 64); \
    break;
      ATOMIC_BINOP_CASE(Add, Add_d)
      ATOMIC_BINOP_CASE(Sub, Sub_d)
      ATOMIC_BINOP_CASE(And, And)
      ATOMIC_BINOP_CASE(Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor)
#undef ATOMIC_BINOP_CASE
    case kLa64AssertEqual:
      __ Assert(eq, static_cast<AbortReason>(i.InputOperand(2).immediate()),
                i.InputRegister(0), Operand(i.InputRegister(1)));
      break;
    case kLa64S128Const:
    case kLa64S128Zero:
    case kLa64I32x4Splat:
    case kLa64I32x4ExtractLane:
    case kLa64I32x4AddHoriz:
    case kLa64I32x4Add:
    case kLa64I32x4ReplaceLane:
    case kLa64I32x4Sub:
    case kLa64F64x2Abs:
    default:
      break;
  }
  return kSuccess;
}  // NOLINT(readability/fn_size)

#define UNSUPPORTED_COND(opcode, condition)                                    \
  StdoutStream{} << "Unsupported " << #opcode << " condition: \"" << condition \
                 << "\"";                                                      \
  UNIMPLEMENTED();

void AssembleBranchToLabels(CodeGenerator* gen, TurboAssembler* tasm,
                            Instruction* instr, FlagsCondition condition,
                            Label* tlabel, Label* flabel, bool fallthru) {
#undef __
#define __ tasm->
  La64OperandConverter i(gen, instr);

  Condition cc = kNoCondition;
  // LOONG64 does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit loong64 pseudo-instructions, which are handled here by branch
  // instructions that do the actual comparison. Essential that the input
  // registers to compare pseudo-op are not modified before this branch op, as
  // they are tested here.

  if (instr->arch_opcode() == kLa64Tst) {
    cc = FlagsConditionToConditionTst(condition);
    __ Branch(tlabel, cc, kScratchReg, Operand(zero_reg));
  } else if (instr->arch_opcode() == kLa64Dadd ||
             instr->arch_opcode() == kLa64Dsub) {
    cc = FlagsConditionToConditionOvf(condition);
    __ srai_d(kScratchReg, i.OutputRegister(), 32);
    __ srai_w(kScratchReg2, i.OutputRegister(), 31);
    __ Branch(tlabel, cc, kScratchReg2, Operand(kScratchReg));
  } else if (instr->arch_opcode() == kLa64DaddOvf ||
             instr->arch_opcode() == kLa64DsubOvf) {
    switch (condition) {
      // Overflow occurs if overflow register is negative
      case kOverflow:
        __ Branch(tlabel, lt, kScratchReg, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, ge, kScratchReg, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(instr->arch_opcode(), condition);
        break;
    }
  } else if (instr->arch_opcode() == kLa64MulOvf) {
    // Overflow occurs if overflow register is not zero
    switch (condition) {
      case kOverflow:
        __ Branch(tlabel, ne, kScratchReg, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, eq, kScratchReg, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(kLa64MulOvf, condition);
        break;
    }
  } else if (instr->arch_opcode() == kLa64Cmp) {
    cc = FlagsConditionToConditionCmp(condition);
    __ Branch(tlabel, cc, i.InputRegister(0), i.InputOperand(1));
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    cc = FlagsConditionToConditionCmp(condition);
    Register lhs_register = sp;
    uint32_t offset;
    if (gen->ShouldApplyOffsetToStackCheck(instr, &offset)) {
      lhs_register = i.TempRegister(0);
      __ Sub_d(lhs_register, sp, offset);
    }
    __ Branch(tlabel, cc, lhs_register, Operand(i.InputRegister(0)));
  } else if (instr->arch_opcode() == kLa64CmpS ||
             instr->arch_opcode() == kLa64CmpD) {
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    if (predicate) {
      __ BranchTrueF(tlabel);
    } else {
      __ BranchFalseF(tlabel);
    }
  } else {
    PrintF("AssembleArchBranch Unimplemented arch_opcode: %d\n",
           instr->arch_opcode());
    UNIMPLEMENTED();
  }
  if (!fallthru) __ Branch(flabel);  // no fallthru to flabel.
#undef __
#define __ tasm()->
}

// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;

  AssembleBranchToLabels(this, tasm(), instr, branch->condition, tlabel, flabel,
                         branch->fallthru);
}

void CodeGenerator::AssembleBranchPoisoning(FlagsCondition condition,
                                            Instruction* instr) {
  // TODO(jarin) Handle float comparisons (kUnordered[Not]Equal).
  if (condition == kUnorderedEqual || condition == kUnorderedNotEqual) {
    return;
  }

  La64OperandConverter i(this, instr);
  condition = NegateFlagsCondition(condition);

  switch (instr->arch_opcode()) {
    case kLa64Cmp: {
      __ LoadZeroOnCondition(kSpeculationPoisonRegister, i.InputRegister(0),
                             i.InputOperand(1),
                             FlagsConditionToConditionCmp(condition));
    }
      return;
    case kLa64Tst: {
      switch (condition) {
        case kEqual:
          __ LoadZeroIfConditionZero(kSpeculationPoisonRegister, kScratchReg);
          break;
        case kNotEqual:
          __ LoadZeroIfConditionNotZero(kSpeculationPoisonRegister,
                                        kScratchReg);
          break;
        default:
          UNREACHABLE();
      }
    }
      return;
    case kLa64Dadd:
    case kLa64Dsub: {
      // Check for overflow creates 1 or 0 for result.
      __ srli_d(kScratchReg, i.OutputRegister(), 63);
      __ srli_w(kScratchReg2, i.OutputRegister(), 31);
      __ xor_(kScratchReg2, kScratchReg, kScratchReg2);
      switch (condition) {
        case kOverflow:
          __ LoadZeroIfConditionNotZero(kSpeculationPoisonRegister,
                                        kScratchReg2);
          break;
        case kNotOverflow:
          __ LoadZeroIfConditionZero(kSpeculationPoisonRegister, kScratchReg2);
          break;
        default:
          UNSUPPORTED_COND(instr->arch_opcode(), condition);
      }
    }
      return;
    case kLa64DaddOvf:
    case kLa64DsubOvf: {
      // Overflow occurs if overflow register is negative
      __ Slt(kScratchReg2, kScratchReg, zero_reg);
      switch (condition) {
        case kOverflow:
          __ LoadZeroIfConditionNotZero(kSpeculationPoisonRegister,
                                        kScratchReg2);
          break;
        case kNotOverflow:
          __ LoadZeroIfConditionZero(kSpeculationPoisonRegister, kScratchReg2);
          break;
        default:
          UNSUPPORTED_COND(instr->arch_opcode(), condition);
      }
    }
      return;
    case kLa64MulOvf: {
      // Overflow occurs if overflow register is not zero
      switch (condition) {
        case kOverflow:
          __ LoadZeroIfConditionNotZero(kSpeculationPoisonRegister,
                                        kScratchReg);
          break;
        case kNotOverflow:
          __ LoadZeroIfConditionZero(kSpeculationPoisonRegister, kScratchReg);
          break;
        default:
          UNSUPPORTED_COND(instr->arch_opcode(), condition);
      }
    }
      return;
    case kLa64CmpS:
    case kLa64CmpD: {
      bool predicate;
      FlagsConditionToConditionCmpFPU(&predicate, condition);
      if (predicate) {
        __ LoadZeroIfFPUCondition(kSpeculationPoisonRegister);
      } else {
        __ LoadZeroIfNotFPUCondition(kSpeculationPoisonRegister);
      }
    }
      return;
    default:
      UNREACHABLE();
  }
}

#undef UNSUPPORTED_COND

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJump(RpoNumber target) {
  if (!IsNextInAssemblyOrder(target)) __ Branch(GetLabel(target));
}

void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  class OutOfLineTrap final : public OutOfLineCode {
   public:
    OutOfLineTrap(CodeGenerator* gen, Instruction* instr)
        : OutOfLineCode(gen), instr_(instr), gen_(gen) {}
    void Generate() final {
      La64OperandConverter i(gen_, instr_);
      TrapId trap_id =
          static_cast<TrapId>(i.InputInt32(instr_->InputCount() - 1));
      GenerateCallToTrap(trap_id);
    }

   private:
    void GenerateCallToTrap(TrapId trap_id) {
      if (trap_id == TrapId::kInvalid) {
        // We cannot test calls to the runtime in cctest/test-run-wasm.
        // Therefore we emit a call to C here instead of a call to the runtime.
        // We use the context register as the scratch register, because we do
        // not have a context here.
        __ PrepareCallCFunction(0, 0, cp);
        __ CallCFunction(
            ExternalReference::wasm_call_trap_callback_for_testing(), 0);
        __ LeaveFrame(StackFrame::WASM);
        auto call_descriptor = gen_->linkage()->GetIncomingDescriptor();
        int pop_count =
            static_cast<int>(call_descriptor->StackParameterCount());
        pop_count += (pop_count & 1);  // align
        __ Drop(pop_count);
        __ Ret();
      } else {
        gen_->AssembleSourcePosition(instr_);
        // A direct call to a wasm runtime stub defined in this module.
        // Just encode the stub index. This will be patched when the code
        // is added to the native module and copied into wasm code space.
        __ Call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
        ReferenceMap* reference_map =
            gen_->zone()->New<ReferenceMap>(gen_->zone());
        gen_->RecordSafepoint(reference_map, Safepoint::kNoLazyDeopt);
        if (FLAG_debug_code) {
          __ stop();
        }
      }
    }
    Instruction* instr_;
    CodeGenerator* gen_;
  };
  auto ool = zone()->New<OutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  AssembleBranchToLabels(this, tasm(), instr, condition, tlabel, nullptr, true);
}

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  La64OperandConverter i(this, instr);

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register result = i.OutputRegister(instr->OutputCount() - 1);
  Condition cc = kNoCondition;
  // La64 does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit loong64 pseudo-instructions, which are checked and handled here.

  if (instr->arch_opcode() == kLa64Tst) {
    cc = FlagsConditionToConditionTst(condition);
    if (cc == eq) {
      __ Sltu(result, kScratchReg, 1);
    } else {
      __ Sltu(result, zero_reg, kScratchReg);
    }
    return;
  } else if (instr->arch_opcode() == kLa64Dadd ||
             instr->arch_opcode() == kLa64Dsub) {
    cc = FlagsConditionToConditionOvf(condition);
    // Check for overflow creates 1 or 0 for result.
    __ srli_d(kScratchReg, i.OutputRegister(), 63);
    __ srli_w(kScratchReg2, i.OutputRegister(), 31);
    __ xor_(result, kScratchReg, kScratchReg2);
    if (cc == eq)  // Toggle result for not overflow.
      __ xori(result, result, 1);
    return;
  } else if (instr->arch_opcode() == kLa64DaddOvf ||
             instr->arch_opcode() == kLa64DsubOvf) {
    // Overflow occurs if overflow register is negative
    __ slt(result, kScratchReg, zero_reg);
  } else if (instr->arch_opcode() == kLa64MulOvf) {
    // Overflow occurs if overflow register is not zero
    __ Sgtu(result, kScratchReg, zero_reg);
  } else if (instr->arch_opcode() == kLa64Cmp) {
    cc = FlagsConditionToConditionCmp(condition);
    switch (cc) {
      case eq:
      case ne: {
        Register left = i.InputRegister(0);
        Operand right = i.InputOperand(1);
        if (instr->InputAt(1)->IsImmediate()) {
          if (is_int12(-right.immediate())) {
            if (right.immediate() == 0) {
              if (cc == eq) {
                __ Sltu(result, left, 1);
              } else {
                __ Sltu(result, zero_reg, left);
              }
            } else {
              __ Add_d(result, left, Operand(-right.immediate()));
              if (cc == eq) {
                __ Sltu(result, result, 1);
              } else {
                __ Sltu(result, zero_reg, result);
              }
            }
          } else {
            if (is_uint12(right.immediate())) {
              __ Xor(result, left, right);
            } else {
              __ li(kScratchReg, right);
              __ Xor(result, left, kScratchReg);
            }
            if (cc == eq) {
              __ Sltu(result, result, 1);
            } else {
              __ Sltu(result, zero_reg, result);
            }
          }
        } else {
          __ Xor(result, left, right);
          if (cc == eq) {
            __ Sltu(result, result, 1);
          } else {
            __ Sltu(result, zero_reg, result);
          }
        }
      } break;
      case lt:
      case ge: {
        Register left = i.InputRegister(0);
        Operand right = i.InputOperand(1);
        __ Slt(result, left, right);
        if (cc == ge) {
          __ xori(result, result, 1);
        }
      } break;
      case gt:
      case le: {
        Register left = i.InputRegister(1);
        Operand right = i.InputOperand(0);
        __ Slt(result, left, right);
        if (cc == le) {
          __ xori(result, result, 1);
        }
      } break;
      case lo:
      case hs: {
        Register left = i.InputRegister(0);
        Operand right = i.InputOperand(1);
        __ Sltu(result, left, right);
        if (cc == hs) {
          __ xori(result, result, 1);
        }
      } break;
      case hi:
      case ls: {
        Register left = i.InputRegister(1);
        Operand right = i.InputOperand(0);
        __ Sltu(result, left, right);
        if (cc == ls) {
          __ xori(result, result, 1);
        }
      } break;
      default:
        UNREACHABLE();
    }
    return;
  } else if (instr->arch_opcode() == kLa64CmpD ||
             instr->arch_opcode() == kLa64CmpS) {
    FPURegister left = i.InputOrZeroDoubleRegister(0);
    FPURegister right = i.InputOrZeroDoubleRegister(1);
    if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
        !__ IsDoubleZeroRegSet()) {
      __ Move(kDoubleRegZero, 0.0);
    }
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    {
      __ movcf2gr(result, FCC0);
      if (!predicate) {
        __ xori(result, result, 1);
      }
    }
    return;
  } else {
    PrintF("AssembleArchBranch Unimplemented arch_opcode is : %d\n",
           instr->arch_opcode());
    TRACE_UNIMPL();
    UNIMPLEMENTED();
  }
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  La64OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  La64OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;

  __ Branch(GetLabel(i.InputRpo(1)), hs, input, Operand(case_count));
  __ GenerateSwitchTable(input, case_count, [&i, this](size_t index) {
    return GetLabel(i.InputRpo(index + 2));
  });
}

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const RegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();
  if (saves_fpu != 0) {
    int count = base::bits::CountPopulation(saves_fpu);
    DCHECK_EQ(kNumCalleeSavedFPU, count);
    frame->AllocateSavedCalleeRegisterSlots(count *
                                            (kDoubleSize / kSystemPointerSize));
  }

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (saves != 0) {
    int count = base::bits::CountPopulation(saves);
    DCHECK_EQ(kNumCalleeSaved, count + 1);
    frame->AllocateSavedCalleeRegisterSlots(count);
  }
}

void CodeGenerator::AssembleConstructFrame() {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  if (frame_access_state()->has_frame()) {
    if (call_descriptor->IsCFunctionCall()) {
      if (info()->GetOutputStackFrameType() == StackFrame::C_WASM_ENTRY) {
        __ StubPrologue(StackFrame::C_WASM_ENTRY);
        // Reserve stack space for saving the c_entry_fp later.
        __ Sub_d(sp, sp, Operand(kSystemPointerSize));
      } else {
        __ Push(ra, fp);
        __ mov(fp, sp);
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      __ StubPrologue(info()->GetOutputStackFrameType());
      if (call_descriptor->IsWasmFunctionCall()) {
        __ Push(kWasmInstanceRegister);
      } else if (call_descriptor->IsWasmImportWrapper() ||
                 call_descriptor->IsWasmCapiFunction()) {
        // Wasm import wrappers are passed a tuple in the place of the instance.
        // Unpack the tuple into the instance and the target callable.
        // This must be done here in the codegen because it cannot be expressed
        // properly in the graph.
        __ Ld_d(kJSFunctionRegister,
                FieldMemOperand(kWasmInstanceRegister, Tuple2::kValue2Offset));
        __ Ld_d(kWasmInstanceRegister,
                FieldMemOperand(kWasmInstanceRegister, Tuple2::kValue1Offset));
        __ Push(kWasmInstanceRegister);
        if (call_descriptor->IsWasmCapiFunction()) {
          // Reserve space for saving the PC later.
          __ Sub_d(sp, sp, Operand(kSystemPointerSize));
        }
      }
    }
  }

  int required_slots =
      frame()->GetTotalFrameSlotCount() - frame()->GetFixedSlotCount();

  if (info()->is_osr()) {
    // TurboFan OSR-compiled functions cannot be entered directly.
    __ Abort(AbortReason::kShouldNotDirectlyEnterOsrFunction);

    // Unoptimized code jumps directly to this entrypoint while the unoptimized
    // frame is still on the stack. Optimized code uses OSR values directly from
    // the unoptimized frame. Thus, all that needs to be done is to allocate the
    // remaining stack slots.
    if (FLAG_code_comments) __ RecordComment("-- OSR entrypoint --");
    osr_pc_offset_ = __ pc_offset();
    required_slots -= osr_helper()->UnoptimizedFrameSlots();
    ResetSpeculationPoison();
  }

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  const RegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();

  if (required_slots > 0) {
    DCHECK(frame_access_state()->has_frame());
    if (info()->IsWasm() && required_slots > 128) {
      // For WebAssembly functions with big frames we have to do the stack
      // overflow check before we construct the frame. Otherwise we may not
      // have enough space on the stack to call the runtime for the stack
      // overflow.
      Label done;

      // If the frame is bigger than the stack, we throw the stack overflow
      // exception unconditionally. Thereby we can avoid the integer overflow
      // check in the condition code.
      if ((required_slots * kSystemPointerSize) < (FLAG_stack_size * 1024)) {
        __ Ld_d(
            kScratchReg,
            FieldMemOperand(kWasmInstanceRegister,
                            WasmInstanceObject::kRealStackLimitAddressOffset));
        __ Ld_d(kScratchReg, MemOperand(kScratchReg, 0));
        __ Add_d(kScratchReg, kScratchReg,
                 Operand(required_slots * kSystemPointerSize));
        __ Branch(&done, uge, sp, Operand(kScratchReg));
      }

      __ Call(wasm::WasmCode::kWasmStackOverflow, RelocInfo::WASM_STUB_CALL);
      // We come from WebAssembly, there are no references for the GC.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map, Safepoint::kNoLazyDeopt);
      if (FLAG_debug_code) {
        __ stop();
      }

      __ bind(&done);
    }
  }

  const int returns = frame()->GetReturnSlotCount();

  // Skip callee-saved and return slots, which are pushed below.
  required_slots -= base::bits::CountPopulation(saves);
  required_slots -= base::bits::CountPopulation(saves_fpu);
  required_slots -= returns;
  if (required_slots > 0) {
    __ Sub_d(sp, sp, Operand(required_slots * kSystemPointerSize));
  }

  if (saves_fpu != 0) {
    // Save callee-saved FPU registers.
    __ MultiPushFPU(saves_fpu);
    DCHECK_EQ(kNumCalleeSavedFPU, base::bits::CountPopulation(saves_fpu));
  }

  if (saves != 0) {
    // Save callee-saved registers.
    __ MultiPush(saves);
    DCHECK_EQ(kNumCalleeSaved, base::bits::CountPopulation(saves) + 1);
  }

  if (returns != 0) {
    // Create space for returns.
    __ Sub_d(sp, sp, Operand(returns * kSystemPointerSize));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* pop) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    __ Add_d(sp, sp, Operand(returns * kSystemPointerSize));
  }

  // Restore GP registers.
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (saves != 0) {
    __ MultiPop(saves);
  }

  // Restore FPU registers.
  const RegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();
  if (saves_fpu != 0) {
    __ MultiPopFPU(saves_fpu);
  }

  La64OperandConverter g(this, nullptr);
  if (call_descriptor->IsCFunctionCall()) {
    AssembleDeconstructFrame();
  } else if (frame_access_state()->has_frame()) {
    // Canonicalize JSFunction return sites for now unless they have an variable
    // number of stack slot pops.
    if (pop->IsImmediate() && g.ToConstant(pop).ToInt32() == 0) {
      if (return_label_.is_bound()) {
        __ Branch(&return_label_);
        return;
      } else {
        __ bind(&return_label_);
        AssembleDeconstructFrame();
      }
    } else {
      AssembleDeconstructFrame();
    }
  }
  int pop_count = static_cast<int>(call_descriptor->StackParameterCount());
  if (pop->IsImmediate()) {
    pop_count += g.ToConstant(pop).ToInt32();
  } else {
    Register pop_reg = g.ToRegister(pop);
    __ slli_d(pop_reg, pop_reg, kSystemPointerSizeLog2);
    __ Add_d(sp, sp, pop_reg);
  }
  if (pop_count != 0) {
    __ DropAndRet(pop_count);
  } else {
    __ Ret();
  }
}

void CodeGenerator::FinishCode() {}

void CodeGenerator::PrepareForDeoptimizationExits(int deopt_count) {}

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  La64OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ mov(g.ToRegister(destination), src);
    } else {
      __ St_d(src, g.ToMemOperand(destination));
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      __ Ld_d(g.ToRegister(destination), src);
    } else {
      Register temp = kScratchReg;
      __ Ld_d(temp, src);
      __ St_d(temp, g.ToMemOperand(destination));
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst =
          destination->IsRegister() ? g.ToRegister(destination) : kScratchReg;
      switch (src.type()) {
        case Constant::kInt32:
          __ li(dst, Operand(src.ToInt32()));
          break;
        case Constant::kFloat32:
          __ li(dst, Operand::EmbeddedNumber(src.ToFloat32()));
          break;
        case Constant::kInt64:
          if (RelocInfo::IsWasmReference(src.rmode())) {
            __ li(dst, Operand(src.ToInt64(), src.rmode()));
          } else {
            __ li(dst, Operand(src.ToInt64()));
          }
          break;
        case Constant::kFloat64:
          __ li(dst, Operand::EmbeddedNumber(src.ToFloat64().value()));
          break;
        case Constant::kExternalReference:
          __ li(dst, src.ToExternalReference());
          break;
        case Constant::kDelayedStringConstant:
          __ li(dst, src.ToDelayedStringConstant());
          break;
        case Constant::kHeapObject: {
          Handle<HeapObject> src_object = src.ToHeapObject();
          RootIndex index;
          if (IsMaterializableFromRoot(src_object, &index)) {
            __ LoadRoot(dst, index);
          } else {
            __ li(dst, src_object);
          }
          break;
        }
        case Constant::kCompressedHeapObject:
          UNREACHABLE();
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(titzer): loading RPO numbers on LOONG64.
          break;
      }
      if (destination->IsStackSlot()) __ St_d(dst, g.ToMemOperand(destination));
    } else if (src.type() == Constant::kFloat32) {
      if (destination->IsFPStackSlot()) {
        MemOperand dst = g.ToMemOperand(destination);
        if (bit_cast<int32_t>(src.ToFloat32()) == 0) {
          __ St_d(zero_reg, dst);
        } else {
          __ li(kScratchReg, Operand(bit_cast<int32_t>(src.ToFloat32())));
          __ St_d(kScratchReg, dst);
        }
      } else {
        DCHECK(destination->IsFPRegister());
        FloatRegister dst = g.ToSingleRegister(destination);
        __ Move(dst, src.ToFloat32());
      }
    } else {
      DCHECK_EQ(Constant::kFloat64, src.type());
      DoubleRegister dst = destination->IsFPRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      __ Move(dst, src.ToFloat64().value());
      if (destination->IsFPStackSlot()) {
        __ Fst_d(dst, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPRegister()) {
    FPURegister src = g.ToDoubleRegister(source);
    if (destination->IsFPRegister()) {
      FPURegister dst = g.ToDoubleRegister(destination);
      __ Move(dst, src);
    } else {
      DCHECK(destination->IsFPStackSlot());
      __ Fst_d(src, g.ToMemOperand(destination));
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsFPRegister()) {
      __ Fld_d(g.ToDoubleRegister(destination), src);
    } else {
      DCHECK(destination->IsFPStackSlot());
      FPURegister temp = kScratchDoubleReg;
      __ Fld_d(temp, src);
      __ Fst_d(temp, g.ToMemOperand(destination));
    }
  } else {
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  La64OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    // Register-register.
    Register temp = kScratchReg;
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ Move(temp, src);
      __ Move(src, dst);
      __ Move(dst, temp);
    } else {
      DCHECK(destination->IsStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ mov(temp, src);
      __ Ld_d(src, dst);
      __ St_d(temp, dst);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsStackSlot());
    Register temp_0 = kScratchReg;
    Register temp_1 = kScratchReg2;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ Ld_d(temp_0, src);
    __ Ld_d(temp_1, dst);
    __ St_d(temp_0, dst);
    __ St_d(temp_1, src);
  } else if (source->IsFPRegister()) {
    FPURegister temp = kScratchDoubleReg;
    FPURegister src = g.ToDoubleRegister(source);
    if (destination->IsFPRegister()) {
      FPURegister dst = g.ToDoubleRegister(destination);
      __ Move(temp, src);
      __ Move(src, dst);
      __ Move(dst, temp);
    } else {
      DCHECK(destination->IsFPStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ Move(temp, src);
      __ Fld_d(src, dst);
      __ Fst_d(temp, dst);
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPStackSlot());
    Register temp_0 = kScratchReg;
    MemOperand src0 = g.ToMemOperand(source);
    MemOperand src1(src0.base(), src0.offset() + kIntSize);
    MemOperand dst0 = g.ToMemOperand(destination);
    MemOperand dst1(dst0.base(), dst0.offset() + kIntSize);
    FPURegister temp_1 = kScratchDoubleReg;
    __ Fld_d(temp_1, dst0);  // Save destination in temp_1.
    __ Ld_w(temp_0, src0);   // Then use temp_0 to copy source to destination.
    __ St_w(temp_0, dst0);
    __ Ld_w(temp_0, src1);
    __ St_w(temp_0, dst1);
    __ Fst_d(temp_1, src0);
  } else {
    // No other combinations are possible.
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  // On 64-bit LOONG64 we emit the jump tables inline.
  UNREACHABLE();
}

#undef ASSEMBLE_ATOMIC_LOAD_INTEGER
#undef ASSEMBLE_ATOMIC_STORE_INTEGER
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_ATOMIC_BINOP_EXT
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT
#undef ASSEMBLE_IEEE754_BINOP
#undef ASSEMBLE_IEEE754_UNOP

#undef TRACE_MSG
#undef TRACE_UNIMPL
#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
