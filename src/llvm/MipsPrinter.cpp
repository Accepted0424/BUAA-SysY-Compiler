#include "include/asm/MipsPrinter.h"

#include <algorithm>
#include <cctype>
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

struct FrameInfo {
    std::unordered_map<const Value*, int> valueOffsets;
    std::unordered_map<const Value*, int> allocaOffsets;
    std::unordered_map<const Argument*, int> argOffsets;
    std::unordered_map<const BasicBlock*, std::string> blockLabels;
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

    FrameInfo buildFrameInfo(const FunctionPtr &func, const std::string &funcLabelPrefix) {
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
                if (needsValueSlot(inst->getValueType())) {
                    nextOffset += 4;
                    info.valueOffsets[inst.get()] = -nextOffset;
                }
            }
        }

        info.frameSize = alignTo4(nextOffset);

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
        FrameInfo frame = buildFrameInfo(func, funcName);
        const std::string retLabel = funcName + "_ret";

        out_ << "\n" << funcName << ":\n";
        out_ << "  addi $sp, $sp, -" << frame.frameSize << "\n";
        out_ << "  sw $ra, " << frame.frameSize - 4 << "($sp)\n";
        out_ << "  sw $fp, " << frame.frameSize - 8 << "($sp)\n";
        out_ << "  addi $fp, $sp, " << frame.frameSize << "\n";

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            out_ << frame.blockLabels[bb.get()] << ":\n";
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                emitInstruction(*instIt, frame, retLabel);
            }
        }

        out_ << retLabel << ":\n";
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

    void loadValue(const ValuePtr &value, const FrameInfo &frame, const std::string &reg) {
        if (!value) {
            out_ << "  li " << reg << ", 0\n";
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
                out_ << "  la $t9, " << label << "\n";
                out_ << "  lw " << reg << ", 0($t9)\n";
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
        auto it = frame.valueOffsets.find(value.get());
        if (it != frame.valueOffsets.end()) {
            out_ << "  sw " << reg << ", " << it->second << "($fp)\n";
        }
    }

    void emitStore(const std::shared_ptr<StoreInst> &inst, const FrameInfo &frame) {
        loadValue(inst->getValueOperand(), frame, "$t0");
        loadAddress(inst->getAddressOperand(), frame, "$t1");
        out_ << "  sw $t0, 0($t1)\n";
    }

    void emitLoad(const std::shared_ptr<LoadInst> &inst, const FrameInfo &frame) {
        loadAddress(inst->getAddressOperand(), frame, "$t1");
        out_ << "  lw $t0, 0($t1)\n";
        storeValue(inst, frame, "$t0");
    }

    void emitBinary(const std::shared_ptr<BinaryOperator> &inst, const FrameInfo &frame) {
        loadValue(inst->lhs_, frame, "$t0");
        loadValue(inst->rhs_, frame, "$t1");
        switch (inst->OpType()) {
            case BinaryOpType::ADD:
                out_ << "  addu $t2, $t0, $t1\n";
                break;
            case BinaryOpType::SUB:
                out_ << "  subu $t2, $t0, $t1\n";
                break;
            case BinaryOpType::MUL:
                out_ << "  mul $t2, $t0, $t1\n";
                break;
            case BinaryOpType::DIV:
                out_ << "  div $t0, $t1\n";
                out_ << "  mflo $t2\n";
                break;
            case BinaryOpType::MOD:
                out_ << "  div $t0, $t1\n";
                out_ << "  mfhi $t2\n";
                break;
        }
        storeValue(inst, frame, "$t2");
    }

    void emitCompare(const std::shared_ptr<CompareOperator> &inst, const FrameInfo &frame) {
        loadValue(inst->getLhs(), frame, "$t0");
        loadValue(inst->getRhs(), frame, "$t1");
        switch (inst->OpType()) {
            case CompareOpType::EQL:
                out_ << "  xor $t2, $t0, $t1\n";
                out_ << "  sltiu $t2, $t2, 1\n";
                break;
            case CompareOpType::NEQ:
                out_ << "  xor $t2, $t0, $t1\n";
                out_ << "  sltu $t2, $zero, $t2\n";
                break;
            case CompareOpType::LSS:
                out_ << "  slt $t2, $t0, $t1\n";
                break;
            case CompareOpType::GRE:
                out_ << "  slt $t2, $t1, $t0\n";
                break;
            case CompareOpType::LEQ:
                out_ << "  slt $t2, $t1, $t0\n";
                out_ << "  xori $t2, $t2, 1\n";
                break;
            case CompareOpType::GEQ:
                out_ << "  slt $t2, $t0, $t1\n";
                out_ << "  xori $t2, $t2, 1\n";
                break;
        }
        storeValue(inst, frame, "$t2");
    }

    void emitLogical(const std::shared_ptr<LogicalOperator> &inst, const FrameInfo &frame) {
        loadValue(inst->getLhs(), frame, "$t0");
        out_ << "  sltu $t0, $zero, $t0\n";
        loadValue(inst->getRhs(), frame, "$t1");
        out_ << "  sltu $t1, $zero, $t1\n";
        if (inst->OpType() == LogicalOpType::AND) {
            out_ << "  and $t2, $t0, $t1\n";
        } else {
            out_ << "  or $t2, $t0, $t1\n";
        }
        storeValue(inst, frame, "$t2");
    }

    void emitZExt(const std::shared_ptr<ZExtInst> &inst, const FrameInfo &frame) {
        loadValue(inst->getOperand(), frame, "$t0");
        out_ << "  sltu $t2, $zero, $t0\n";
        storeValue(inst, frame, "$t2");
    }

    void emitUnary(const std::shared_ptr<UnaryOperator> &inst, const FrameInfo &frame) {
        loadValue(inst->getOperand(), frame, "$t0");
        switch (inst->OpType()) {
            case UnaryOpType::POS:
                out_ << "  move $t2, $t0\n";
                break;
            case UnaryOpType::NEG:
                out_ << "  subu $t2, $zero, $t0\n";
                break;
            case UnaryOpType::NOT:
                out_ << "  sltiu $t2, $t0, 1\n";
                break;
        }
        storeValue(inst, frame, "$t2");
    }

    void emitCall(const std::shared_ptr<CallInst> &inst, const FrameInfo &frame) {
        const auto funcName = sanitizeName(inst->getFunction()->getName());
        const auto &args = inst->getArgs();
        // push arguments in reverse order
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            auto arg = *it;
            const bool isPointer = arg && arg->getType() && arg->getType()->is(Type::ArrayTyID);
            if (isPointer) {
                loadAddress(arg, frame, "$t0");
            } else {
                loadValue(arg, frame, "$t0");
            }
            out_ << "  addi $sp, $sp, -4\n";
            out_ << "  sw $t0, 0($sp)\n";
        }
        out_ << "  jal " << funcName << "\n";
        emitNop();
        if (!args.empty()) {
            out_ << "  addi $sp, $sp, " << static_cast<int>(args.size()) * 4 << "\n";
        }
        const bool hasRet = inst->getType() && !inst->getType()->is(Type::VoidTyID);
        if (hasRet) {
            storeValue(inst, frame, "$v0");
        }
    }

    void emitGEP(const std::shared_ptr<GetElementPtrInst> &inst, const FrameInfo &frame) {
        loadAddress(inst->getAddressOperand(), frame, "$t0");
        int immOffset = 0;
        bool hasRegOffset = false;
        TypePtr curType = inst->getAddressOperand() ? inst->getAddressOperand()->getType() : nullptr;

        for (const auto &idx : inst->getIndices()) {
            const int stride = elementStride(curType);
            if (idx->getValueType() == ValueType::ConstantIntTy) {
                auto ci = std::static_pointer_cast<ConstantInt>(idx);
                immOffset += ci->getValue() * stride;
            } else {
                loadValue(idx, frame, "$t1");
                if (stride == 1) {
                    out_ << "  move $t1, $t1\n";
                } else if ((stride & (stride - 1)) == 0) {
                    int shift = 0;
                    int s = stride;
                    while (s > 1) {
                        ++shift;
                        s >>= 1;
                    }
                    out_ << "  sll $t1, $t1, " << shift << "\n";
                } else {
                    out_ << "  li $t3, " << stride << "\n";
                    out_ << "  mul $t1, $t1, $t3\n";
                }

                if (!hasRegOffset) {
                    out_ << "  move $t2, $t1\n";
                    hasRegOffset = true;
                } else {
                    out_ << "  addu $t2, $t2, $t1\n";
                }
            }

            if (curType && curType->is(Type::ArrayTyID)) {
                curType = std::static_pointer_cast<ArrayType>(curType)->getElementType();
            }
        }

        if (immOffset != 0) {
            out_ << "  addi $t0, $t0, " << immOffset << "\n";
        }
        if (hasRegOffset) {
            out_ << "  addu $t0, $t0, $t2\n";
        }
        storeValue(inst, frame, "$t0");
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
        loadValue(inst->getCondition(), frame, "$t0");
        auto tLabel = frame.blockLabels.at(inst->getTrueBlock().get());
        auto fLabel = frame.blockLabels.at(inst->getFalseBlock().get());
        out_ << "  bne $t0, $zero, " << tLabel << "\n";
        emitNop();
        out_ << "  j " << fLabel << "\n";
        emitNop();
    }
};

void MipsPrinter::print() const {
    MipsPrinterImpl impl(module_, out_);
    impl.print();
}
