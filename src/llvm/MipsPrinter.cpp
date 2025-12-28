#include "include/asm/MipsPrinter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "logger.h"
#include "ir/type.h"
#include "ir/value/Argument.h"
#include "ir/value/BasicBlock.h"
#include "ir/value/ConstantArray.h"
#include "ir/value/ConstantInt.h"
#include "ir/value/Function.h"
#include "ir/value/GlobalVariable.h"
#include "ir/value/inst/BinaryInstruction.h"
#include "ir/value/inst/Instruction.h"
#include "ir/value/inst/UnaryInstruction.h"

namespace {

int alignTo4(int size) {
    return (size + 3) / 4 * 4;
}

std::string sanitizeName(const std::string &name) {
    std::string res = name;
    for (char &ch : res) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
            ch = '_';
        }
    }
    if (res.empty()) {
        res = "label";
    }
    return res;
}

int typeSize(const TypePtr &type) {
    if (!type) {
        return 4;
    }
    if (type->is(Type::IntegerTyID)) {
        return 4;
    }
    if (type->is(Type::ArrayTyID)) {
        auto arr = std::static_pointer_cast<ArrayType>(type);
        if (arr->getElementNum() < 0) {
            return 4;  // treat pointer-like arrays as a single word
        }
        return arr->getElementNum() * typeSize(arr->getElementType());
    }
    return 4;
}

int elementStride(const TypePtr &type) {
    if (type && type->is(Type::ArrayTyID)) {
        auto arr = std::static_pointer_cast<ArrayType>(type);
        return typeSize(arr->getElementType());
    }
    return typeSize(type);
}

bool needsValueSlot(const ValueType vt) {
    switch (vt) {
        case ValueType::BinaryOperatorTy:
        case ValueType::CompareInstTy:
        case ValueType::LogicalInstTy:
        case ValueType::ZExtInstTy:
        case ValueType::CallInstTy:
        case ValueType::LoadInstTy:
        case ValueType::UnaryOperatorTy:
        case ValueType::GetElementPtrInstTy:
            return true;
        default:
            return false;
    }
}

class TempRegPool {
public:
    TempRegPool() {
        for (int i = 0; i <= 9; ++i) {
            regs_.push_back("$t" + std::to_string(i));
        }
    }

    std::string acquire() {
        if (regs_.empty()) {
            return "$t0";  // fallback
        }
        const auto reg = regs_.back();
        regs_.pop_back();
        inUse_.insert(reg);
        return reg;
    }

    void release(const std::string &reg) {
        if (inUse_.erase(reg)) {
            regs_.push_back(reg);
        }
    }

private:
    std::vector<std::string> regs_;
    std::unordered_set<std::string> inUse_;
};

struct RegisterPlan {
    std::unordered_map<const Value*, std::string> valueRegs;
    std::vector<std::string> calleeSaved;
    std::unordered_map<std::string, int> calleeSavedOffsets;
};

struct FrameInfo {
    std::unordered_map<const Value*, int> valueOffsets;
    std::unordered_map<const Value*, int> allocaOffsets;
    std::unordered_map<const Argument*, int> argOffsets;
    std::unordered_map<const BasicBlock*, std::string> blockLabels;
    RegisterPlan regs;
    int frameSize = 8;  // reserve space for $fp and $ra
};

}  // namespace

class MipsPrinterImpl {
public:
    MipsPrinterImpl(Module &module, std::ostream &out)
        : module_(module), out_(out) {}

    void print() {
        emitData();
        out_ << "\n.text\n";

        auto mainFunc = module_.getMainFunction();
        if (mainFunc) {
            emitStartStub(mainFunc->getName());
        }
        emitBuiltins();

        std::unordered_set<const Function*> printed;
        for (auto it = module_.functionBegin(); it != module_.functionEnd(); ++it) {
            emitFunction(*it);
            printed.insert(it->get());
        }
        if (mainFunc && printed.count(mainFunc.get()) == 0) {
            emitFunction(mainFunc);
        }
    }

private:
    Module &module_;
    std::ostream &out_;
    TempRegPool temps_;

    static bool allocatableValue(const ValuePtr &v) {
        if (!v) return false;
        switch (v->getValueType()) {
            case ValueType::ArgumentTy:
            case ValueType::BinaryOperatorTy:
            case ValueType::CompareInstTy:
            case ValueType::LogicalInstTy:
            case ValueType::ZExtInstTy:
            case ValueType::CallInstTy:
            case ValueType::LoadInstTy:
            case ValueType::UnaryOperatorTy:
            case ValueType::GetElementPtrInstTy:
                return true;
            default:
                return false;
        }
    }

    RegisterPlan planRegisters(const FunctionPtr &func) {
        std::unordered_map<const Value*, int> useCount;
        auto consider = [&](const ValuePtr &v) {
            if (!v) return;
            if (!allocatableValue(v)) return;
            const int count = v->getUseCount();
            if (count > 0) {
                useCount[v.get()] = count;
            }
        };

        for (const auto &arg : func->getArgs()) {
            consider(arg);
        }
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                consider(*instIt);
            }
        }

        std::vector<std::pair<const Value*, int>> sorted;
        sorted.reserve(useCount.size());
        for (const auto &[ptr, cnt] : useCount) {
            sorted.emplace_back(ptr, cnt);
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            if (a.second != b.second) return a.second > b.second;
            return reinterpret_cast<uintptr_t>(a.first) < reinterpret_cast<uintptr_t>(b.first);
        });

        RegisterPlan plan;
        std::vector<std::string> avail = {"$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7"};
        for (const auto &[val, cnt] : sorted) {
            if (cnt <= 1 || avail.empty()) continue;
            const auto reg = avail.back();
            avail.pop_back();
            plan.valueRegs[val] = reg;
            plan.calleeSaved.push_back(reg);
        }

        return plan;
    }

    void emitNop() {
        out_ << "  nop\n";
    }

    void emitData() {
        out_ << ".data\n";
        for (auto it = module_.globalVarBegin(); it != module_.globalVarEnd(); ++it) {
            auto gv = std::static_pointer_cast<GlobalVariable>(*it);
            const auto label = sanitizeName(gv->getName());
            out_ << label << ":\n";
            if (gv->getType()->is(Type::ArrayTyID)) {
                auto arrType = std::static_pointer_cast<ArrayType>(gv->getType());
                const int elemNum = arrType->getElementNum();
                const int totalBytes = std::max(0, elemNum) * 4;
                if (gv->value_ && gv->value_->getValueType() == ValueType::ConstantArrayTy) {
                    auto constArr = std::static_pointer_cast<ConstantArray>(gv->value_);
                    out_ << "  .word ";
                    const auto &elems = constArr->getElements();
                    for (size_t idx = 0; idx < elems.size(); ++idx) {
                        if (idx > 0) out_ << ", ";
                        out_ << elems[idx]->getValue();
                    }
                    out_ << "\n";
                } else {
                    out_ << "  .space " << totalBytes << "\n";
                }
            } else {
                int initVal = 0;
                if (gv->value_ && gv->value_->getValueType() == ValueType::ConstantIntTy) {
                    initVal = std::static_pointer_cast<ConstantInt>(gv->value_)->getValue();
                }
                out_ << "  .word " << initVal << "\n";
            }
        }
    }

    void emitBuiltins() {
        emitGetint();
        emitPutint();
        emitPutch();
        emitPutstr();
    }

    void emitStartStub(const std::string &mainNameRaw) {
        const std::string mainName = sanitizeName(mainNameRaw);
        out_ << "\n.globl _start\n"
             << "_start:\n"
             << "  jal " << mainName << "\n"
             << "  nop\n"
             << "  li $v0, 10\n"
             << "  syscall\n";
    }

    void emitBuiltinPrologue(const std::string &name, int frameSize = 8) {
        out_ << "\n.globl " << name << "\n" << name << ":\n";
        out_ << "  addi $sp, $sp, -" << frameSize << "\n";
        out_ << "  sw $ra, " << frameSize - 4 << "($sp)\n";
        out_ << "  sw $fp, " << frameSize - 8 << "($sp)\n";
        out_ << "  addi $fp, $sp, " << frameSize << "\n";
    }

    void emitBuiltinEpilogue(int frameSize = 8) {
        out_ << "  lw $ra, " << frameSize - 4 << "($sp)\n";
        out_ << "  lw $fp, " << frameSize - 8 << "($sp)\n";
        out_ << "  addi $sp, $sp, " << frameSize << "\n";
        out_ << "  jr $ra\n";
        emitNop();
    }

    void emitGetint() {
        const int frameSize = 8;
        emitBuiltinPrologue("getint", frameSize);
        out_ << "  li $v0, 5\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    void emitPutint() {
        const int frameSize = 8;
        emitBuiltinPrologue("putint", frameSize);
        out_ << "  lw $a0, 0($fp)\n";
        out_ << "  li $v0, 1\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    void emitPutch() {
        const int frameSize = 8;
        emitBuiltinPrologue("putch", frameSize);
        out_ << "  lw $a0, 0($fp)\n";
        out_ << "  li $v0, 11\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    void emitPutstr() {
        const int frameSize = 8;
        emitBuiltinPrologue("putstr", frameSize);
        out_ << "  lw $a0, 0($fp)\n";
        out_ << "  li $v0, 4\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    FrameInfo buildFrameInfo(const FunctionPtr &func, const std::string &funcLabelPrefix, const RegisterPlan &plan) {
        FrameInfo info;
        int nextOffset = 8;  // keep space for $ra and $fp near the top of the frame

        int argIdx = 0;
        for (const auto &arg : func->getArgs()) {
            info.argOffsets[arg.get()] = argIdx * 4;
            ++argIdx;
        }

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                auto inst = *instIt;
                if (inst->getValueType() == ValueType::AllocaInstTy) {
                    const int sz = typeSize(inst->getType());
                    nextOffset += sz;
                    info.allocaOffsets[inst.get()] = -nextOffset;
                    continue;
                }
                const bool pinned = plan.valueRegs.count(inst.get()) > 0;
                if (needsValueSlot(inst->getValueType()) && !pinned && inst->getUseCount() > 0) {
                    nextOffset += 4;
                    info.valueOffsets[inst.get()] = -nextOffset;
                }
            }
        }

        nextOffset += static_cast<int>(plan.calleeSaved.size()) * 4;
        info.frameSize = alignTo4(nextOffset);
        info.regs = plan;
        int saveOffset = 12;
        for (const auto &reg : plan.calleeSaved) {
            info.regs.calleeSavedOffsets[reg] = info.frameSize - saveOffset;
            saveOffset += 4;
        }

        int bbId = 0;
        std::unordered_set<std::string> usedLabels;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            std::string name = bb->getName().empty() ? ("bb" + std::to_string(bbId++)) : bb->getName();
            name = sanitizeName(name);
            std::string unique = name;
            int suffix = 1;
            while (usedLabels.count(unique)) {
                unique = name + "_" + std::to_string(suffix++);
            }
            usedLabels.insert(unique);
            std::string label = funcLabelPrefix + "_" + unique;
            if (label == funcLabelPrefix + "_ret") {
                label += "_bb";
            }
            info.blockLabels[bb.get()] = label;
        }

        return info;
    }

    void emitFunction(const FunctionPtr &func) {
        const auto funcName = sanitizeName(func->getName());
        auto regPlan = planRegisters(func);
        FrameInfo frame = buildFrameInfo(func, funcName, regPlan);
        const std::string retLabel = funcName + "_ret";

        out_ << "\n" << funcName << ":\n";
        out_ << "  addi $sp, $sp, -" << frame.frameSize << "\n";
        out_ << "  sw $ra, " << frame.frameSize - 4 << "($sp)\n";
        out_ << "  sw $fp, " << frame.frameSize - 8 << "($sp)\n";
        out_ << "  addi $fp, $sp, " << frame.frameSize << "\n";
        for (const auto &reg : frame.regs.calleeSaved) {
            const int offset = frame.regs.calleeSavedOffsets.at(reg);
            out_ << "  sw " << reg << ", " << offset << "($sp)\n";
        }
        // seed callee-saved registers for frequently used arguments
        for (const auto &arg : func->getArgs()) {
            auto it = frame.regs.valueRegs.find(arg.get());
            if (it != frame.regs.valueRegs.end()) {
                const int offset = frame.argOffsets[arg.get()];
                out_ << "  lw " << it->second << ", " << offset << "($fp)\n";
            }
        }

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            out_ << frame.blockLabels[bb.get()] << ":\n";
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                emitInstruction(*instIt, frame, retLabel);
            }
        }

        out_ << retLabel << ":\n";
        for (const auto &reg : frame.regs.calleeSaved) {
            const int offset = frame.regs.calleeSavedOffsets.at(reg);
            out_ << "  lw " << reg << ", " << offset << "($sp)\n";
        }
        out_ << "  lw $ra, " << frame.frameSize - 4 << "($sp)\n";
        out_ << "  lw $fp, " << frame.frameSize - 8 << "($sp)\n";
        out_ << "  addi $sp, $sp, " << frame.frameSize << "\n";
        out_ << "  jr $ra\n";
        emitNop();
    }

    void emitInstruction(const InstructionPtr &inst, const FrameInfo &frame, const std::string &retLabel) {
        switch (inst->getValueType()) {
            case ValueType::AllocaInstTy:
                break;
            case ValueType::StoreInstTy:
                emitStore(std::static_pointer_cast<StoreInst>(inst), frame);
                break;
            case ValueType::LoadInstTy:
                emitLoad(std::static_pointer_cast<LoadInst>(inst), frame);
                break;
            case ValueType::BinaryOperatorTy:
                emitBinary(std::static_pointer_cast<BinaryOperator>(inst), frame);
                break;
            case ValueType::CompareInstTy:
                emitCompare(std::static_pointer_cast<CompareOperator>(inst), frame);
                break;
            case ValueType::LogicalInstTy:
                emitLogical(std::static_pointer_cast<LogicalOperator>(inst), frame);
                break;
            case ValueType::ZExtInstTy:
                emitZExt(std::static_pointer_cast<ZExtInst>(inst), frame);
                break;
            case ValueType::UnaryOperatorTy:
                emitUnary(std::static_pointer_cast<UnaryOperator>(inst), frame);
                break;
            case ValueType::CallInstTy:
                emitCall(std::static_pointer_cast<CallInst>(inst), frame);
                break;
            case ValueType::GetElementPtrInstTy:
                emitGEP(std::static_pointer_cast<GetElementPtrInst>(inst), frame);
                break;
            case ValueType::ReturnInstTy:
                emitReturn(std::static_pointer_cast<ReturnInst>(inst), frame, retLabel);
                break;
            case ValueType::JumpInstTy:
                emitJump(std::static_pointer_cast<JumpInst>(inst), frame);
                break;
            case ValueType::BranchInstTy:
                emitBranch(std::static_pointer_cast<BranchInst>(inst), frame);
                break;
            default:
                out_ << "  # unsupported instruction\n";
                break;
        }
    }

    std::string regForValue(const ValuePtr &value, const FrameInfo &frame) {
        if (!value) return "";
        auto it = frame.regs.valueRegs.find(value.get());
        if (it != frame.regs.valueRegs.end()) {
            return it->second;
        }
        return "";
    }

    struct TargetReg {
        std::string name;
        bool isTemp = false;
    };

    TargetReg acquireTargetReg(const ValuePtr &value, const FrameInfo &frame) {
        TargetReg target;
        auto mapped = regForValue(value, frame);
        if (!mapped.empty()) {
            target.name = mapped;
            target.isTemp = false;
        } else {
            target.name = temps_.acquire();
            target.isTemp = true;
        }
        return target;
    }

    void releaseTarget(const TargetReg &target) {
        if (target.isTemp) {
            temps_.release(target.name);
        }
    }

    void loadValue(const ValuePtr &value, const FrameInfo &frame, const std::string &reg) {
        if (!value) {
            out_ << "  li " << reg << ", 0\n";
            return;
        }

        const auto mapped = regForValue(value, frame);
        if (!mapped.empty()) {
            if (mapped != reg) {
                out_ << "  move " << reg << ", " << mapped << "\n";
            }
            return;
        }

        switch (value->getValueType()) {
            case ValueType::ConstantIntTy: {
                auto ci = std::static_pointer_cast<ConstantInt>(value);
                out_ << "  li " << reg << ", " << ci->getValue() << "\n";
                return;
            }
            case ValueType::ArgumentTy: {
                auto arg = std::static_pointer_cast<Argument>(value);
                auto it = frame.argOffsets.find(arg.get());
                if (it != frame.argOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                } else {
                    out_ << "  li " << reg << ", 0\n";
                }
                return;
            }
            case ValueType::GlobalVariableTy: {
                auto gv = std::static_pointer_cast<GlobalVariable>(value);
                const auto label = sanitizeName(gv->getName());
                out_ << "  la " << reg << ", " << label << "\n";
                out_ << "  lw " << reg << ", 0(" << reg << ")\n";
                return;
            }
            case ValueType::AllocaInstTy: {
                auto it = frame.allocaOffsets.find(value.get());
                if (it != frame.allocaOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                } else {
                    out_ << "  li " << reg << ", 0\n";
                }
                return;
            }
            default:
                break;
        }

        auto it = frame.valueOffsets.find(value.get());
        if (it != frame.valueOffsets.end()) {
            out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
        } else {
            out_ << "  li " << reg << ", 0\n";
        }
    }

    void loadAddress(const ValuePtr &value, const FrameInfo &frame, const std::string &reg) {
        if (!value) {
            out_ << "  move " << reg << ", $zero\n";
            return;
        }
        const auto mapped = regForValue(value, frame);
        if (!mapped.empty()) {
            if (mapped != reg) {
                out_ << "  move " << reg << ", " << mapped << "\n";
            }
            return;
        }
        switch (value->getValueType()) {
            case ValueType::AllocaInstTy: {
                auto it = frame.allocaOffsets.find(value.get());
                const int offset = (it != frame.allocaOffsets.end()) ? it->second : 0;
                out_ << "  addi " << reg << ", $fp, " << offset << "\n";
                return;
            }
            case ValueType::GlobalVariableTy: {
                auto gv = std::static_pointer_cast<GlobalVariable>(value);
                out_ << "  la " << reg << ", " << sanitizeName(gv->getName()) << "\n";
                return;
            }
            case ValueType::GetElementPtrInstTy:
            case ValueType::CallInstTy:
            case ValueType::BinaryOperatorTy:
            case ValueType::CompareInstTy:
            case ValueType::LogicalInstTy:
            case ValueType::ZExtInstTy:
            case ValueType::UnaryOperatorTy:
            case ValueType::LoadInstTy: {
                auto it = frame.valueOffsets.find(value.get());
                if (it != frame.valueOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                } else {
                    out_ << "  move " << reg << ", $zero\n";
                }
                return;
            }
            case ValueType::ArgumentTy: {
                auto arg = std::static_pointer_cast<Argument>(value);
                auto it = frame.argOffsets.find(arg.get());
                if (it != frame.argOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                } else {
                    out_ << "  move " << reg << ", $zero\n";
                }
                return;
            }
            default:
                out_ << "  move " << reg << ", $zero\n";
                return;
        }
    }

    void storeValue(const ValuePtr &value, const FrameInfo &frame, const std::string &reg) {
        if (!value || value->getUseCount() == 0) {
            return;
        }
        auto it = frame.valueOffsets.find(value.get());
        if (it != frame.valueOffsets.end()) {
            out_ << "  sw " << reg << ", " << it->second << "($fp)\n";
        }
    }

    void emitStore(const std::shared_ptr<StoreInst> &inst, const FrameInfo &frame) {
        auto valReg = temps_.acquire();
        auto addrReg = temps_.acquire();
        loadValue(inst->getValueOperand(), frame, valReg);
        loadAddress(inst->getAddressOperand(), frame, addrReg);
        out_ << "  sw " << valReg << ", 0(" << addrReg << ")\n";
        temps_.release(valReg);
        temps_.release(addrReg);
    }

    void emitLoad(const std::shared_ptr<LoadInst> &inst, const FrameInfo &frame) {
        auto addrReg = temps_.acquire();
        auto dst = acquireTargetReg(inst, frame);
        loadAddress(inst->getAddressOperand(), frame, addrReg);
        out_ << "  lw " << dst.name << ", 0(" << addrReg << ")\n";
        storeValue(inst, frame, dst.name);
        temps_.release(addrReg);
        releaseTarget(dst);
    }

    void emitBinary(const std::shared_ptr<BinaryOperator> &inst, const FrameInfo &frame) {
        auto isConstInt = [](const ValuePtr &value, int &out) -> bool {
            if (value && value->getValueType() == ValueType::ConstantIntTy) {
                out = std::static_pointer_cast<ConstantInt>(value)->getValue();
                return true;
            }
            return false;
        };
        auto fitsImm16 = [](int imm) -> bool {
            return imm >= -32768 && imm <= 32767;
        };

        int lhsImm = 0;
        int rhsImm = 0;
        const bool lhsIsImm = isConstInt(inst->lhs_, lhsImm);
        const bool rhsIsImm = isConstInt(inst->rhs_, rhsImm);
        auto dst = acquireTargetReg(inst, frame);

        if (inst->OpType() == BinaryOpType::ADD && rhsIsImm && fitsImm16(rhsImm)) {
            auto lhsReg = temps_.acquire();
            loadValue(inst->lhs_, frame, lhsReg);
            out_ << "  addiu " << dst.name << ", " << lhsReg << ", " << rhsImm << "\n";
            temps_.release(lhsReg);
            storeValue(inst, frame, dst.name);
            releaseTarget(dst);
            return;
        }
        if (inst->OpType() == BinaryOpType::ADD && lhsIsImm && fitsImm16(lhsImm)) {
            auto rhsReg = temps_.acquire();
            loadValue(inst->rhs_, frame, rhsReg);
            out_ << "  addiu " << dst.name << ", " << rhsReg << ", " << lhsImm << "\n";
            temps_.release(rhsReg);
            storeValue(inst, frame, dst.name);
            releaseTarget(dst);
            return;
        }
        if (inst->OpType() == BinaryOpType::SUB && rhsIsImm && fitsImm16(-rhsImm)) {
            auto lhsReg = temps_.acquire();
            loadValue(inst->lhs_, frame, lhsReg);
            out_ << "  addiu " << dst.name << ", " << lhsReg << ", " << -rhsImm << "\n";
            temps_.release(lhsReg);
            storeValue(inst, frame, dst.name);
            releaseTarget(dst);
            return;
        }

        auto lhsReg = temps_.acquire();
        auto rhsReg = temps_.acquire();
        loadValue(inst->lhs_, frame, lhsReg);
        loadValue(inst->rhs_, frame, rhsReg);
        switch (inst->OpType()) {
            case BinaryOpType::ADD:
                out_ << "  addu " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                break;
            case BinaryOpType::SUB:
                out_ << "  subu " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                break;
            case BinaryOpType::MUL:
                out_ << "  mul " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                break;
            case BinaryOpType::DIV:
                out_ << "  div " << lhsReg << ", " << rhsReg << "\n";
                out_ << "  mflo " << dst.name << "\n";
                break;
            case BinaryOpType::MOD:
                out_ << "  div " << lhsReg << ", " << rhsReg << "\n";
                out_ << "  mfhi " << dst.name << "\n";
                break;
        }
        storeValue(inst, frame, dst.name);
        temps_.release(lhsReg);
        temps_.release(rhsReg);
        releaseTarget(dst);
    }

    void emitCompare(const std::shared_ptr<CompareOperator> &inst, const FrameInfo &frame) {
        auto isConstInt = [](const ValuePtr &value, int &out) -> bool {
            if (value && value->getValueType() == ValueType::ConstantIntTy) {
                out = std::static_pointer_cast<ConstantInt>(value)->getValue();
                return true;
            }
            return false;
        };
        int rhsImm = 0;
        const bool rhsIsZero = isConstInt(inst->getRhs(), rhsImm) && rhsImm == 0;

        auto lhsReg = temps_.acquire();
        loadValue(inst->getLhs(), frame, lhsReg);
        auto dst = acquireTargetReg(inst, frame);
        if (rhsIsZero && inst->OpType() == CompareOpType::EQL) {
            out_ << "  sltiu " << dst.name << ", " << lhsReg << ", 1\n";
        } else if (rhsIsZero && inst->OpType() == CompareOpType::NEQ) {
            out_ << "  sltu " << dst.name << ", $zero, " << lhsReg << "\n";
        } else {
            auto rhsReg = temps_.acquire();
            loadValue(inst->getRhs(), frame, rhsReg);
            switch (inst->OpType()) {
                case CompareOpType::EQL:
                    out_ << "  xor " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                    out_ << "  sltiu " << dst.name << ", " << dst.name << ", 1\n";
                    break;
                case CompareOpType::NEQ:
                    out_ << "  xor " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                    out_ << "  sltu " << dst.name << ", $zero, " << dst.name << "\n";
                    break;
                case CompareOpType::LSS:
                    out_ << "  slt " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                    break;
                case CompareOpType::GRE:
                    out_ << "  slt " << dst.name << ", " << rhsReg << ", " << lhsReg << "\n";
                    break;
                case CompareOpType::LEQ:
                    out_ << "  slt " << dst.name << ", " << rhsReg << ", " << lhsReg << "\n";
                    out_ << "  xori " << dst.name << ", " << dst.name << ", 1\n";
                    break;
                case CompareOpType::GEQ:
                    out_ << "  slt " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
                    out_ << "  xori " << dst.name << ", " << dst.name << ", 1\n";
                    break;
            }
            temps_.release(rhsReg);
        }
        storeValue(inst, frame, dst.name);
        temps_.release(lhsReg);
        releaseTarget(dst);
    }

    void emitLogical(const std::shared_ptr<LogicalOperator> &inst, const FrameInfo &frame) {
        auto lhsReg = temps_.acquire();
        auto rhsReg = temps_.acquire();
        loadValue(inst->getLhs(), frame, lhsReg);
        out_ << "  sltu " << lhsReg << ", $zero, " << lhsReg << "\n";
        loadValue(inst->getRhs(), frame, rhsReg);
        out_ << "  sltu " << rhsReg << ", $zero, " << rhsReg << "\n";
        auto dst = acquireTargetReg(inst, frame);
        if (inst->OpType() == LogicalOpType::AND) {
            out_ << "  and " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
        } else {
            out_ << "  or " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
        }
        storeValue(inst, frame, dst.name);
        temps_.release(lhsReg);
        temps_.release(rhsReg);
        releaseTarget(dst);
    }

    void emitZExt(const std::shared_ptr<ZExtInst> &inst, const FrameInfo &frame) {
        auto srcReg = temps_.acquire();
        loadValue(inst->getOperand(), frame, srcReg);
        auto dst = acquireTargetReg(inst, frame);
        out_ << "  sltu " << dst.name << ", $zero, " << srcReg << "\n";
        storeValue(inst, frame, dst.name);
        temps_.release(srcReg);
        releaseTarget(dst);
    }

    void emitUnary(const std::shared_ptr<UnaryOperator> &inst, const FrameInfo &frame) {
        auto opReg = temps_.acquire();
        loadValue(inst->getOperand(), frame, opReg);
        auto dst = acquireTargetReg(inst, frame);
        switch (inst->OpType()) {
            case UnaryOpType::POS:
                out_ << "  move " << dst.name << ", " << opReg << "\n";
                break;
            case UnaryOpType::NEG:
                out_ << "  subu " << dst.name << ", $zero, " << opReg << "\n";
                break;
            case UnaryOpType::NOT:
                out_ << "  sltiu " << dst.name << ", " << opReg << ", 1\n";
                break;
        }
        storeValue(inst, frame, dst.name);
        temps_.release(opReg);
        releaseTarget(dst);
    }

    void emitCall(const std::shared_ptr<CallInst> &inst, const FrameInfo &frame) {
        const auto funcName = sanitizeName(inst->getFunction()->getName());
        const auto &args = inst->getArgs();
        // push arguments in reverse order
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            auto arg = *it;
            const bool isPointer = arg && arg->getType() && arg->getType()->is(Type::ArrayTyID);
            auto argReg = temps_.acquire();
            if (isPointer) {
                loadAddress(arg, frame, argReg);
            } else {
                loadValue(arg, frame, argReg);
            }
            out_ << "  addi $sp, $sp, -4\n";
            out_ << "  sw " << argReg << ", 0($sp)\n";
            temps_.release(argReg);
        }
        out_ << "  jal " << funcName << "\n";
        emitNop();
        if (!args.empty()) {
            out_ << "  addi $sp, $sp, " << static_cast<int>(args.size()) * 4 << "\n";
        }
        const bool hasRet = inst->getType() && !inst->getType()->is(Type::VoidTyID);
        if (hasRet) {
            auto dst = acquireTargetReg(inst, frame);
            if (dst.name != "$v0") {
                out_ << "  move " << dst.name << ", $v0\n";
            }
            storeValue(inst, frame, dst.name);
            releaseTarget(dst);
        }
    }

    void emitGEP(const std::shared_ptr<GetElementPtrInst> &inst, const FrameInfo &frame) {
        auto baseReg = temps_.acquire();
        loadAddress(inst->getAddressOperand(), frame, baseReg);
        int immOffset = 0;
        bool hasRegOffset = false;
        std::string regOffset;
        TypePtr curType = inst->getAddressOperand() ? inst->getAddressOperand()->getType() : nullptr;

        for (const auto &idx : inst->getIndices()) {
            const int stride = elementStride(curType);
            if (idx->getValueType() == ValueType::ConstantIntTy) {
                auto ci = std::static_pointer_cast<ConstantInt>(idx);
                immOffset += ci->getValue() * stride;
            } else {
                auto idxReg = temps_.acquire();
                loadValue(idx, frame, idxReg);
                if (stride == 1) {
                    out_ << "  move " << idxReg << ", " << idxReg << "\n";
                } else if ((stride & (stride - 1)) == 0) {
                    int shift = 0;
                    int s = stride;
                    while (s > 1) {
                        ++shift;
                        s >>= 1;
                    }
                    out_ << "  sll " << idxReg << ", " << idxReg << ", " << shift << "\n";
                } else {
                    auto mulReg = temps_.acquire();
                    out_ << "  li " << mulReg << ", " << stride << "\n";
                    out_ << "  mul " << idxReg << ", " << idxReg << ", " << mulReg << "\n";
                    temps_.release(mulReg);
                }

                if (!hasRegOffset) {
                    regOffset = idxReg;
                    hasRegOffset = true;
                } else {
                    out_ << "  addu " << regOffset << ", " << regOffset << ", " << idxReg << "\n";
                    temps_.release(idxReg);
                }
            }

            if (curType && curType->is(Type::ArrayTyID)) {
                curType = std::static_pointer_cast<ArrayType>(curType)->getElementType();
            }
        }

        if (immOffset != 0) {
            out_ << "  addi " << baseReg << ", " << baseReg << ", " << immOffset << "\n";
        }
        if (hasRegOffset) {
            out_ << "  addu " << baseReg << ", " << baseReg << ", " << regOffset << "\n";
            temps_.release(regOffset);
        }
        auto dst = acquireTargetReg(inst, frame);
        if (dst.name != baseReg) {
            out_ << "  move " << dst.name << ", " << baseReg << "\n";
        }
        storeValue(inst, frame, dst.name);
        releaseTarget(dst);
        temps_.release(baseReg);
    }

    void emitReturn(const std::shared_ptr<ReturnInst> &inst, const FrameInfo &frame, const std::string &retLabel) {
        if (inst->getReturnValue()) {
            loadValue(inst->getReturnValue(), frame, "$v0");
        }
        out_ << "  j " << retLabel << "\n";
        emitNop();
    }

    void emitJump(const std::shared_ptr<JumpInst> &inst, const FrameInfo &frame) {
        auto target = inst->getTarget();
        out_ << "  j " << frame.blockLabels.at(target.get()) << "\n";
        emitNop();
    }

    void emitBranch(const std::shared_ptr<BranchInst> &inst, const FrameInfo &frame) {
        auto condReg = temps_.acquire();
        loadValue(inst->getCondition(), frame, condReg);
        auto tLabel = frame.blockLabels.at(inst->getTrueBlock().get());
        auto fLabel = frame.blockLabels.at(inst->getFalseBlock().get());
        out_ << "  bne " << condReg << ", $zero, " << tLabel << "\n";
        emitNop();
        out_ << "  j " << fLabel << "\n";
        emitNop();
        temps_.release(condReg);
    }
};

void MipsPrinter::print() const {
    MipsPrinterImpl impl(module_, out_);
    impl.print();
}
