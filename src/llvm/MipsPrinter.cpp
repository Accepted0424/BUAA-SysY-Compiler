#include "include/asm/MipsPrinter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
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

void collectOperands(const InstructionPtr &inst, std::vector<ValuePtr> &ops) {
    switch (inst->getValueType()) {
        case ValueType::BinaryOperatorTy:
        case ValueType::CompareInstTy:
        case ValueType::LogicalInstTy: {
            auto bin = std::static_pointer_cast<BinaryInstruction>(inst);
            ops.push_back(bin->lhs_);
            ops.push_back(bin->rhs_);
            break;
        }
        case ValueType::ZExtInstTy: {
            auto zext = std::static_pointer_cast<ZExtInst>(inst);
            ops.push_back(zext->getOperand());
            break;
        }
        case ValueType::UnaryOperatorTy: {
            auto un = std::static_pointer_cast<UnaryOperator>(inst);
            ops.push_back(un->getOperand());
            break;
        }
        case ValueType::GetElementPtrInstTy: {
            auto gep = std::static_pointer_cast<GetElementPtrInst>(inst);
            ops.push_back(gep->getAddressOperand());
            for (const auto &idx : gep->getIndices()) {
                ops.push_back(idx);
            }
            break;
        }
        case ValueType::LoadInstTy: {
            auto ld = std::static_pointer_cast<LoadInst>(inst);
            ops.push_back(ld->getAddressOperand());
            break;
        }
        case ValueType::StoreInstTy: {
            auto st = std::static_pointer_cast<StoreInst>(inst);
            ops.push_back(st->getValueOperand());
            ops.push_back(st->getAddressOperand());
            break;
        }
        case ValueType::CallInstTy: {
            auto call = std::static_pointer_cast<CallInst>(inst);
            for (const auto &arg : call->getArgs()) {
                ops.push_back(arg);
            }
            break;
        }
        case ValueType::ReturnInstTy: {
            auto ret = std::static_pointer_cast<ReturnInst>(inst);
            if (ret->getReturnValue()) {
                ops.push_back(ret->getReturnValue());
            }
            break;
        }
        case ValueType::BranchInstTy: {
            auto br = std::static_pointer_cast<BranchInst>(inst);
            ops.push_back(br->getCondition());
            break;
        }
        default:
            break;
    }
}

class TempRegPool {
public:
    TempRegPool() {
        for (int i = 0; i <= 6; ++i) {
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
    std::unordered_map<const Argument*, int> callerArgOffsets;
    std::unordered_map<const Argument*, std::string> argRegs;
    std::unordered_map<const BasicBlock*, std::string> blockLabels;
    RegisterPlan regs;
    int frameSize = 8;  // reserve space for $fp and $ra
    bool omitPrologue = false;
    bool hasCall = false;
};

struct BlockRegCache {
    std::array<std::string, 2> regs = {"$t8", "$t9"};
    std::array<const Value*, 2> values = {nullptr, nullptr};
    int nextEvict = 0;

    void reset() {
        values = {nullptr, nullptr};
        nextEvict = 0;
    }

    std::string get(const Value *value) const {
        if (!value) return "";
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                return regs[i];
            }
        }
        return "";
    }

    std::string bind(const Value *value) {
        if (!value) return "";
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                return regs[i];
            }
        }
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i] == nullptr) {
                values[i] = value;
                return regs[i];
            }
        }
        const int idx = nextEvict;
        nextEvict = (nextEvict + 1) % static_cast<int>(values.size());
        values[idx] = value;
        return regs[idx];
    }

    void invalidate(const Value *value) {
        if (!value) return;
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i] == value) {
                values[i] = nullptr;
            }
        }
    }

    void invalidateAll() {
        for (auto &v : values) {
            v = nullptr;
        }
    }
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
    const BasicBlock *curBlock_ = nullptr;
    const Value *curInductionAddr_ = nullptr;
    int loopId_ = 0;

    struct LoopInfo {
        BasicBlockPtr cond;
        BasicBlockPtr body;
        BasicBlockPtr step;
        BasicBlockPtr end;
        ValuePtr addr;
    };

    struct ArrayLoopEmit {
        ValuePtr base;
        ValuePtr delta;
        int startIdx = 0;
        int count = 0;
        int stride = 4;
    };

    std::vector<std::unique_ptr<LoopInfo>> loopInfos_;
    std::unordered_map<const BasicBlock*, const LoopInfo*> loopForBlock_;
    std::unordered_map<const BasicBlock*, const LoopInfo*> loopByCond_;

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

    bool functionHasCall(const FunctionPtr &func) {
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                if ((*instIt)->getValueType() == ValueType::CallInstTy) {
                    return true;
                }
            }
        }
        return false;
    }

    InstructionPtr getTerminator(const BasicBlockPtr &bb) {
        InstructionPtr last = nullptr;
        for (auto it = bb->instructionBegin(); it != bb->instructionEnd(); ++it) {
            last = *it;
        }
        return last;
    }

    void detectLoopInductions(const FunctionPtr &func) {
        loopInfos_.clear();
        loopForBlock_.clear();
        loopByCond_.clear();
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto cond = *bbIt;
            auto term = getTerminator(cond);
            if (!term || term->getValueType() != ValueType::BranchInstTy) {
                continue;
            }
            auto br = std::static_pointer_cast<BranchInst>(term);
            auto body = br->getTrueBlock();
            auto end = br->getFalseBlock();
            if (!body || !end) continue;
            auto bodyTerm = getTerminator(body);
            if (!bodyTerm || bodyTerm->getValueType() != ValueType::JumpInstTy) continue;
            auto step = std::static_pointer_cast<JumpInst>(bodyTerm)->getTarget();
            if (!step) continue;
            auto stepTerm = getTerminator(step);
            if (!stepTerm || stepTerm->getValueType() != ValueType::JumpInstTy) continue;
            if (std::static_pointer_cast<JumpInst>(stepTerm)->getTarget() != cond) continue;

            auto cmp = std::dynamic_pointer_cast<CompareOperator>(br->getCondition());
            if (!cmp) continue;
            ValuePtr addr = nullptr;
            if (auto ld = std::dynamic_pointer_cast<LoadInst>(cmp->getLhs())) {
                addr = ld->getAddressOperand();
            } else if (auto ld = std::dynamic_pointer_cast<LoadInst>(cmp->getRhs())) {
                addr = ld->getAddressOperand();
            }
            if (!addr) continue;

            auto info = std::make_unique<LoopInfo>();
            info->cond = cond;
            info->body = body;
            info->step = step;
            info->end = end;
            info->addr = addr;

            auto raw = info.get();
            loopInfos_.push_back(std::move(info));
            loopForBlock_[cond.get()] = raw;
            loopForBlock_[body.get()] = raw;
            loopForBlock_[step.get()] = raw;
            loopByCond_[cond.get()] = raw;
        }
    }

    bool isZeroConst(const ValuePtr &value) {
        auto ci = std::dynamic_pointer_cast<ConstantInt>(value);
        return ci && ci->getValue() == 0;
    }

    std::optional<ArrayLoopEmit> matchArrayUpdate(const InstructionPtr &inst,
        std::vector<const Instruction*> &parts) {
        if (!inst || inst->getValueType() != ValueType::StoreInstTy) {
            return std::nullopt;
        }
        auto st = std::static_pointer_cast<StoreInst>(inst);
        auto add = std::dynamic_pointer_cast<BinaryOperator>(st->getValueOperand());
        if (!add || add->OpType() != BinaryOpType::ADD) {
            return std::nullopt;
        }
        auto lhsLoad = std::dynamic_pointer_cast<LoadInst>(add->lhs_);
        auto rhsLoad = std::dynamic_pointer_cast<LoadInst>(add->rhs_);
        ValuePtr delta;
        LoadInstPtr load;
        if (lhsLoad) {
            load = lhsLoad;
            delta = add->rhs_;
        } else if (rhsLoad) {
            load = rhsLoad;
            delta = add->lhs_;
        } else {
            return std::nullopt;
        }
        if (!load) return std::nullopt;
        if (load->getAddressOperand() != st->getAddressOperand()) {
            return std::nullopt;
        }
        auto gep = std::dynamic_pointer_cast<GetElementPtrInst>(st->getAddressOperand());
        if (!gep) return std::nullopt;
        const auto &indices = gep->getIndices();
        if (indices.empty()) return std::nullopt;
        for (size_t i = 0; i + 1 < indices.size(); ++i) {
            if (!isZeroConst(indices[i])) {
                return std::nullopt;
            }
        }
        auto idxConst = std::dynamic_pointer_cast<ConstantInt>(indices.back());
        if (!idxConst) return std::nullopt;

        ArrayLoopEmit emit;
        emit.base = gep->getAddressOperand();
        emit.delta = delta;
        emit.startIdx = idxConst->getValue();
        emit.count = 1;
        emit.stride = typeSize(gep->getType());

        parts.push_back(load.get());
        parts.push_back(add.get());
        parts.push_back(gep.get());
        parts.push_back(st.get());
        return emit;
    }

    struct ArrayLoopPlan {
        std::unordered_map<const Instruction*, ArrayLoopEmit> emitAt;
        std::unordered_set<const Instruction*> skip;
    };

    ArrayLoopPlan buildArrayLoops(const BasicBlockPtr &bb) {
        ArrayLoopPlan plan;
        std::vector<InstructionPtr> insts;
        std::unordered_map<const Instruction*, size_t> index;
        size_t idx = 0;
        for (auto it = bb->instructionBegin(); it != bb->instructionEnd(); ++it, ++idx) {
            insts.push_back(*it);
            index[(*it).get()] = idx;
        }

        struct Match {
            InstructionPtr store;
            std::vector<const Instruction*> parts;
            ArrayLoopEmit emit;
        };
        std::vector<Match> matches;
        for (const auto &inst : insts) {
            std::vector<const Instruction*> parts;
            auto maybe = matchArrayUpdate(inst, parts);
            if (maybe) {
                matches.push_back({inst, parts, *maybe});
            }
        }

        size_t i = 0;
        while (i < matches.size()) {
            auto &m = matches[i];
            int expected = m.emit.startIdx;
            size_t j = i;
            ArrayLoopEmit cur = m.emit;
            std::vector<const Instruction*> runParts;

            while (j < matches.size()) {
                auto &mj = matches[j];
                if (mj.emit.base.get() != cur.base.get() ||
                    mj.emit.delta.get() != cur.delta.get() ||
                    mj.emit.startIdx != expected) {
                    break;
                }
                runParts.insert(runParts.end(), mj.parts.begin(), mj.parts.end());
                expected += 1;
                j += 1;
            }

            const int runCount = expected - cur.startIdx;
            if (runCount >= 3) {
                const Instruction *emitAt = nullptr;
                size_t emitIndex = std::numeric_limits<size_t>::max();
                for (const auto *part : runParts) {
                    auto it = index.find(part);
                    if (it != index.end() && it->second < emitIndex) {
                        emitIndex = it->second;
                        emitAt = part;
                    }
                }
                if (emitAt) {
                    bool overlaps = false;
                    for (const auto *part : runParts) {
                        if (plan.skip.count(part)) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (!overlaps) {
                        cur.count = runCount;
                        plan.emitAt[emitAt] = cur;
                        for (const auto *part : runParts) {
                            plan.skip.insert(part);
                        }
                    }
                }
            }
            i = j;
        }

        return plan;
    }

    std::unordered_set<const Instruction*> computeSkipInsts(const FunctionPtr &func) {
        std::unordered_set<const Instruction*> skip;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                auto inst = *instIt;
                if (inst->getValueType() != ValueType::BranchInstTy) {
                    continue;
                }
                auto br = std::static_pointer_cast<BranchInst>(inst);
                auto condInst = std::dynamic_pointer_cast<Instruction>(br->getCondition());
                if (!condInst) continue;
                if (condInst->getValueType() == ValueType::CompareInstTy &&
                    condInst->getUseCount() == 1) {
                    skip.insert(condInst.get());
                }
            }
        }
        return skip;
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
        out_ << "  li $v0, 1\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    void emitPutch() {
        const int frameSize = 8;
        emitBuiltinPrologue("putch", frameSize);
        out_ << "  li $v0, 11\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    void emitPutstr() {
        const int frameSize = 8;
        emitBuiltinPrologue("putstr", frameSize);
        out_ << "  li $v0, 4\n";
        out_ << "  syscall\n";
        emitBuiltinEpilogue(frameSize);
    }

    FrameInfo buildFrameInfo(const FunctionPtr &func, const std::string &funcLabelPrefix,
        const RegisterPlan &plan, bool hasCall) {
        FrameInfo info;
        info.hasCall = hasCall;
        int nextOffset = 8;  // keep space for $ra and $fp near the top of the frame

        std::vector<InstructionPtr> instList;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                instList.push_back(*instIt);
            }
        }

        std::unordered_map<const Value*, int> lastUse;
        for (size_t i = 0; i < instList.size(); ++i) {
            std::vector<ValuePtr> ops;
            collectOperands(instList[i], ops);
            for (const auto &op : ops) {
                auto opInst = std::dynamic_pointer_cast<Instruction>(op);
                if (opInst) {
                    lastUse[opInst.get()] = static_cast<int>(i);
                }
            }
        }

        for (const auto &inst : instList) {
            if (inst->getValueType() == ValueType::AllocaInstTy) {
                const int sz = typeSize(inst->getType());
                nextOffset += sz;
                info.allocaOffsets[inst.get()] = -nextOffset;
            }
        }

        int argIdx = 0;
        for (const auto &arg : func->getArgs()) {
            if (argIdx >= 4) {
                info.callerArgOffsets[arg.get()] = (argIdx - 4) * 4;
            }
            if (argIdx < 4 && plan.valueRegs.count(arg.get()) == 0 && arg->getUseCount() > 0) {
                if (!hasCall) {
                    info.argRegs[arg.get()] = "$a" + std::to_string(argIdx);
                } else {
                    nextOffset += 4;
                    info.argOffsets[arg.get()] = -nextOffset;
                }
            }
            ++argIdx;
        }

        std::vector<int> freeSlots;
        std::vector<std::vector<const Value*>> releaseAt(instList.size());
        for (const auto &kv : lastUse) {
            if (kv.second >= 0) {
                releaseAt[kv.second].push_back(kv.first);
            }
        }

        for (size_t i = 0; i < instList.size(); ++i) {
            auto inst = instList[i];
            if (inst->getValueType() == ValueType::AllocaInstTy) {
                continue;
            }
            const bool pinned = plan.valueRegs.count(inst.get()) > 0;
            if (needsValueSlot(inst->getValueType()) && !pinned && inst->getUseCount() > 0) {
                int offset = 0;
                if (!freeSlots.empty()) {
                    offset = freeSlots.back();
                    freeSlots.pop_back();
                } else {
                    nextOffset += 4;
                    offset = -nextOffset;
                }
                info.valueOffsets[inst.get()] = offset;
            }
            for (const auto *val : releaseAt[i]) {
                auto it = info.valueOffsets.find(val);
                if (it != info.valueOffsets.end()) {
                    freeSlots.push_back(it->second);
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

        const bool noStackSlots = info.allocaOffsets.empty() && info.valueOffsets.empty() &&
            info.argOffsets.empty() && plan.calleeSaved.empty();
        const bool noCallerArgs = info.callerArgOffsets.empty();
        if (!hasCall && noStackSlots && noCallerArgs) {
            info.frameSize = 0;
            info.omitPrologue = true;
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
        const bool hasCall = functionHasCall(func);
        FrameInfo frame = buildFrameInfo(func, funcName, regPlan, hasCall);
        detectLoopInductions(func);
        auto skipInsts = computeSkipInsts(func);
        const std::string retLabel = funcName + "_ret";

        out_ << "\n" << funcName << ":\n";
        if (!frame.omitPrologue) {
            out_ << "  addi $sp, $sp, -" << frame.frameSize << "\n";
            if (frame.hasCall) {
                out_ << "  sw $ra, " << frame.frameSize - 4 << "($sp)\n";
            }
            out_ << "  sw $fp, " << frame.frameSize - 8 << "($sp)\n";
            out_ << "  addi $fp, $sp, " << frame.frameSize << "\n";
            for (const auto &reg : frame.regs.calleeSaved) {
                const int offset = frame.regs.calleeSavedOffsets.at(reg);
                out_ << "  sw " << reg << ", " << offset << "($sp)\n";
            }
        }
        // seed callee-saved registers / home slots for arguments
        int argIdx = 0;
        for (const auto &arg : func->getArgs()) {
            auto it = frame.regs.valueRegs.find(arg.get());
            if (it != frame.regs.valueRegs.end()) {
                if (argIdx < 4) {
                    out_ << "  move " << it->second << ", $a" << argIdx << "\n";
                } else {
                    const int offset = frame.callerArgOffsets.at(arg.get());
                    out_ << "  lw " << it->second << ", " << offset << "($fp)\n";
                }
            } else if (argIdx < 4) {
                auto offIt = frame.argOffsets.find(arg.get());
                if (offIt != frame.argOffsets.end()) {
                    out_ << "  sw $a" << argIdx << ", " << offIt->second << "($fp)\n";
                }
            }
            ++argIdx;
        }

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            curBlock_ = bb.get();
            auto loopIt = loopForBlock_.find(bb.get());
            curInductionAddr_ = (loopIt != loopForBlock_.end()) ? loopIt->second->addr.get() : nullptr;
            BlockRegCache cache;
            cache.reset();
            auto arrayLoops = buildArrayLoops(bb);
            out_ << frame.blockLabels[bb.get()] << ":\n";
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                emitInstruction(*instIt, frame, retLabel, cache, skipInsts, arrayLoops);
            }
        }

        out_ << retLabel << ":\n";
        if (!frame.omitPrologue) {
            for (const auto &reg : frame.regs.calleeSaved) {
                const int offset = frame.regs.calleeSavedOffsets.at(reg);
                out_ << "  lw " << reg << ", " << offset << "($sp)\n";
            }
            if (frame.hasCall) {
                out_ << "  lw $ra, " << frame.frameSize - 4 << "($sp)\n";
            }
            out_ << "  lw $fp, " << frame.frameSize - 8 << "($sp)\n";
            out_ << "  addi $sp, $sp, " << frame.frameSize << "\n";
        }
        out_ << "  jr $ra\n";
        emitNop();
        curBlock_ = nullptr;
        curInductionAddr_ = nullptr;
    }

    void emitInstruction(const InstructionPtr &inst, const FrameInfo &frame, const std::string &retLabel,
        BlockRegCache &cache, const std::unordered_set<const Instruction*> &skipInsts,
        const ArrayLoopPlan &arrayLoops) {
        if (arrayLoops.emitAt.count(inst.get()) > 0) {
            emitArrayUpdateLoop(arrayLoops.emitAt.at(inst.get()), frame, cache);
            return;
        }
        if (skipInsts.count(inst.get()) > 0 || arrayLoops.skip.count(inst.get()) > 0) {
            return;
        }
        switch (inst->getValueType()) {
            case ValueType::AllocaInstTy:
                break;
            case ValueType::StoreInstTy:
                emitStore(std::static_pointer_cast<StoreInst>(inst), frame, cache);
                break;
            case ValueType::LoadInstTy:
                emitLoad(std::static_pointer_cast<LoadInst>(inst), frame, cache);
                break;
            case ValueType::BinaryOperatorTy:
                emitBinary(std::static_pointer_cast<BinaryOperator>(inst), frame, cache);
                break;
            case ValueType::CompareInstTy:
                emitCompare(std::static_pointer_cast<CompareOperator>(inst), frame, cache);
                break;
            case ValueType::LogicalInstTy:
                emitLogical(std::static_pointer_cast<LogicalOperator>(inst), frame, cache);
                break;
            case ValueType::ZExtInstTy:
                emitZExt(std::static_pointer_cast<ZExtInst>(inst), frame, cache);
                break;
            case ValueType::UnaryOperatorTy:
                emitUnary(std::static_pointer_cast<UnaryOperator>(inst), frame, cache);
                break;
            case ValueType::CallInstTy:
                emitCall(std::static_pointer_cast<CallInst>(inst), frame, cache);
                break;
            case ValueType::GetElementPtrInstTy:
                emitGEP(std::static_pointer_cast<GetElementPtrInst>(inst), frame, cache);
                break;
            case ValueType::ReturnInstTy:
                emitReturn(std::static_pointer_cast<ReturnInst>(inst), frame, retLabel, cache);
                break;
            case ValueType::JumpInstTy:
                emitJump(std::static_pointer_cast<JumpInst>(inst), frame);
                break;
            case ValueType::BranchInstTy:
                emitBranch(std::static_pointer_cast<BranchInst>(inst), frame, cache);
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

    void loadValue(const ValuePtr &value, const FrameInfo &frame, const std::string &reg, BlockRegCache &cache) {
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

        const auto cached = cache.get(value.get());
        if (!cached.empty()) {
            if (cached != reg) {
                out_ << "  move " << reg << ", " << cached << "\n";
            }
            return;
        }

        bool loaded = false;
        switch (value->getValueType()) {
            case ValueType::ConstantIntTy: {
                auto ci = std::static_pointer_cast<ConstantInt>(value);
                out_ << "  li " << reg << ", " << ci->getValue() << "\n";
                return;
            }
            case ValueType::ArgumentTy: {
                auto arg = std::static_pointer_cast<Argument>(value);
                auto regIt = frame.argRegs.find(arg.get());
                if (regIt != frame.argRegs.end()) {
                    if (regIt->second != reg) {
                        out_ << "  move " << reg << ", " << regIt->second << "\n";
                    }
                    return;
                }
                auto it = frame.argOffsets.find(arg.get());
                if (it != frame.argOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                    loaded = true;
                    break;
                }
                auto callerIt = frame.callerArgOffsets.find(arg.get());
                if (callerIt != frame.callerArgOffsets.end()) {
                    out_ << "  lw " << reg << ", " << callerIt->second << "($fp)\n";
                    loaded = true;
                    break;
                }
                out_ << "  li " << reg << ", 0\n";
                return;
            }
            case ValueType::GlobalVariableTy: {
                auto gv = std::static_pointer_cast<GlobalVariable>(value);
                const auto label = sanitizeName(gv->getName());
                out_ << "  la " << reg << ", " << label << "\n";
                out_ << "  lw " << reg << ", 0(" << reg << ")\n";
                loaded = true;
                break;
            }
            case ValueType::AllocaInstTy: {
                auto it = frame.allocaOffsets.find(value.get());
                if (it != frame.allocaOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                    loaded = true;
                    break;
                }
                out_ << "  li " << reg << ", 0\n";
                return;
            }
            default:
                break;
        }

        if (!loaded) {
            auto it = frame.valueOffsets.find(value.get());
            if (it != frame.valueOffsets.end()) {
                out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                loaded = true;
            } else {
                out_ << "  li " << reg << ", 0\n";
                return;
            }
        }

        if (value->getValueType() != ValueType::ConstantIntTy &&
            regForValue(value, frame).empty() &&
            value->getUseCount() > 1) {
            auto cacheReg = cache.bind(value.get());
            if (!cacheReg.empty() && cacheReg != reg) {
                out_ << "  move " << cacheReg << ", " << reg << "\n";
            }
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
                auto regIt = frame.argRegs.find(arg.get());
                if (regIt != frame.argRegs.end()) {
                    if (regIt->second != reg) {
                        out_ << "  move " << reg << ", " << regIt->second << "\n";
                    }
                    return;
                }
                auto it = frame.argOffsets.find(arg.get());
                if (it != frame.argOffsets.end()) {
                    out_ << "  lw " << reg << ", " << it->second << "($fp)\n";
                    return;
                }
                auto callerIt = frame.callerArgOffsets.find(arg.get());
                if (callerIt != frame.callerArgOffsets.end()) {
                    out_ << "  lw " << reg << ", " << callerIt->second << "($fp)\n";
                    return;
                }
                out_ << "  move " << reg << ", $zero\n";
                return;
            }
            default:
                out_ << "  move " << reg << ", $zero\n";
                return;
        }
    }

    void storeValue(const ValuePtr &value, const FrameInfo &frame, const std::string &reg, BlockRegCache &cache) {
        if (!value || value->getUseCount() == 0) {
            return;
        }
        auto it = frame.valueOffsets.find(value.get());
        if (it != frame.valueOffsets.end()) {
            out_ << "  sw " << reg << ", " << it->second << "($fp)\n";
        }
        if (regForValue(value, frame).empty()) {
            auto cacheReg = cache.bind(value.get());
            if (!cacheReg.empty() && cacheReg != reg) {
                out_ << "  move " << cacheReg << ", " << reg << "\n";
            }
        }
    }

    void emitStore(const std::shared_ptr<StoreInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        auto valReg = temps_.acquire();
        auto addrReg = temps_.acquire();
        loadValue(inst->getValueOperand(), frame, valReg, cache);
        if (curInductionAddr_ && inst->getAddressOperand().get() == curInductionAddr_) {
            out_ << "  move $t7, " << valReg << "\n";
        }
        loadAddress(inst->getAddressOperand(), frame, addrReg);
        out_ << "  sw " << valReg << ", 0(" << addrReg << ")\n";
        temps_.release(valReg);
        temps_.release(addrReg);
    }

    void emitLoad(const std::shared_ptr<LoadInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        if (curInductionAddr_ && inst->getAddressOperand().get() == curInductionAddr_) {
            auto dst = acquireTargetReg(inst, frame);
            if (dst.name != "$t7") {
                out_ << "  move " << dst.name << ", $t7\n";
            }
            storeValue(inst, frame, dst.name, cache);
            releaseTarget(dst);
            return;
        }
        auto addrReg = temps_.acquire();
        auto dst = acquireTargetReg(inst, frame);
        loadAddress(inst->getAddressOperand(), frame, addrReg);
        out_ << "  lw " << dst.name << ", 0(" << addrReg << ")\n";
        storeValue(inst, frame, dst.name, cache);
        temps_.release(addrReg);
        releaseTarget(dst);
    }

    void emitBinary(const std::shared_ptr<BinaryOperator> &inst, const FrameInfo &frame, BlockRegCache &cache) {
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
            loadValue(inst->lhs_, frame, lhsReg, cache);
            out_ << "  addiu " << dst.name << ", " << lhsReg << ", " << rhsImm << "\n";
            temps_.release(lhsReg);
            storeValue(inst, frame, dst.name, cache);
            releaseTarget(dst);
            return;
        }
        if (inst->OpType() == BinaryOpType::ADD && lhsIsImm && fitsImm16(lhsImm)) {
            auto rhsReg = temps_.acquire();
            loadValue(inst->rhs_, frame, rhsReg, cache);
            out_ << "  addiu " << dst.name << ", " << rhsReg << ", " << lhsImm << "\n";
            temps_.release(rhsReg);
            storeValue(inst, frame, dst.name, cache);
            releaseTarget(dst);
            return;
        }
        if (inst->OpType() == BinaryOpType::SUB && rhsIsImm && fitsImm16(-rhsImm)) {
            auto lhsReg = temps_.acquire();
            loadValue(inst->lhs_, frame, lhsReg, cache);
            out_ << "  addiu " << dst.name << ", " << lhsReg << ", " << -rhsImm << "\n";
            temps_.release(lhsReg);
            storeValue(inst, frame, dst.name, cache);
            releaseTarget(dst);
            return;
        }

        auto lhsReg = temps_.acquire();
        auto rhsReg = temps_.acquire();
        loadValue(inst->lhs_, frame, lhsReg, cache);
        loadValue(inst->rhs_, frame, rhsReg, cache);
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
        storeValue(inst, frame, dst.name, cache);
        temps_.release(lhsReg);
        temps_.release(rhsReg);
        releaseTarget(dst);
    }

    void emitAddImmediate(const std::string &dst, const std::string &src, int imm) {
        if (imm >= -32768 && imm <= 32767) {
            out_ << "  addi " << dst << ", " << src << ", " << imm << "\n";
            return;
        }
        auto tmp = temps_.acquire();
        out_ << "  li " << tmp << ", " << imm << "\n";
        out_ << "  addu " << dst << ", " << src << ", " << tmp << "\n";
        temps_.release(tmp);
    }

    void emitArrayUpdateLoop(const ArrayLoopEmit &emit, const FrameInfo &frame, BlockRegCache &cache) {
        const int stride = emit.stride > 0 ? emit.stride : 4;
        auto baseReg = temps_.acquire();
        auto deltaReg = temps_.acquire();
        auto valReg = temps_.acquire();
        auto cntReg = temps_.acquire();

        loadAddress(emit.base, frame, baseReg);
        const int startOffset = emit.startIdx * stride;
        if (startOffset != 0) {
            emitAddImmediate(baseReg, baseReg, startOffset);
        }
        loadValue(emit.delta, frame, deltaReg, cache);
        out_ << "  li " << cntReg << ", " << emit.count << "\n";
        const std::string loopLabel = "loop.opt." + std::to_string(loopId_++);
        out_ << loopLabel << ":\n";
        out_ << "  lw " << valReg << ", 0(" << baseReg << ")\n";
        out_ << "  addu " << valReg << ", " << valReg << ", " << deltaReg << "\n";
        out_ << "  sw " << valReg << ", 0(" << baseReg << ")\n";
        emitAddImmediate(baseReg, baseReg, stride);
        out_ << "  addi " << cntReg << ", " << cntReg << ", -1\n";
        out_ << "  bne " << cntReg << ", $zero, " << loopLabel << "\n";
        emitNop();

        temps_.release(baseReg);
        temps_.release(deltaReg);
        temps_.release(valReg);
        temps_.release(cntReg);
    }

    void emitCompare(const std::shared_ptr<CompareOperator> &inst, const FrameInfo &frame, BlockRegCache &cache) {
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
        loadValue(inst->getLhs(), frame, lhsReg, cache);
        auto dst = acquireTargetReg(inst, frame);
        if (rhsIsZero && inst->OpType() == CompareOpType::EQL) {
            out_ << "  sltiu " << dst.name << ", " << lhsReg << ", 1\n";
        } else if (rhsIsZero && inst->OpType() == CompareOpType::NEQ) {
            out_ << "  sltu " << dst.name << ", $zero, " << lhsReg << "\n";
        } else {
            auto rhsReg = temps_.acquire();
            loadValue(inst->getRhs(), frame, rhsReg, cache);
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
        storeValue(inst, frame, dst.name, cache);
        temps_.release(lhsReg);
        releaseTarget(dst);
    }

    void emitBranchCompare(const std::shared_ptr<CompareOperator> &cmp, const FrameInfo &frame,
        BlockRegCache &cache, const std::string &tLabel, const std::string &fLabel) {
        auto isConstInt = [](const ValuePtr &value, int &out) -> bool {
            if (value && value->getValueType() == ValueType::ConstantIntTy) {
                out = std::static_pointer_cast<ConstantInt>(value)->getValue();
                return true;
            }
            return false;
        };

        int rhsImm = 0;
        const bool rhsIsImm = isConstInt(cmp->getRhs(), rhsImm);
        auto lhsReg = temps_.acquire();
        loadValue(cmp->getLhs(), frame, lhsReg, cache);

        auto rhsReg = temps_.acquire();
        if (rhsIsImm) {
            out_ << "  li " << rhsReg << ", " << rhsImm << "\n";
        } else {
            loadValue(cmp->getRhs(), frame, rhsReg, cache);
        }

        auto tmpReg = temps_.acquire();
        switch (cmp->OpType()) {
            case CompareOpType::EQL:
                out_ << "  beq " << lhsReg << ", " << rhsReg << ", " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
            case CompareOpType::NEQ:
                out_ << "  bne " << lhsReg << ", " << rhsReg << ", " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
            case CompareOpType::LSS:
                out_ << "  slt " << tmpReg << ", " << lhsReg << ", " << rhsReg << "\n";
                out_ << "  bne " << tmpReg << ", $zero, " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
            case CompareOpType::GRE:
                out_ << "  slt " << tmpReg << ", " << rhsReg << ", " << lhsReg << "\n";
                out_ << "  bne " << tmpReg << ", $zero, " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
            case CompareOpType::LEQ:
                out_ << "  slt " << tmpReg << ", " << rhsReg << ", " << lhsReg << "\n";
                out_ << "  beq " << tmpReg << ", $zero, " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
            case CompareOpType::GEQ:
                out_ << "  slt " << tmpReg << ", " << lhsReg << ", " << rhsReg << "\n";
                out_ << "  beq " << tmpReg << ", $zero, " << tLabel << "\n";
                emitNop();
                out_ << "  j " << fLabel << "\n";
                emitNop();
                break;
        }
        temps_.release(tmpReg);
        temps_.release(lhsReg);
        temps_.release(rhsReg);
    }

    void emitLogical(const std::shared_ptr<LogicalOperator> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        auto lhsReg = temps_.acquire();
        auto rhsReg = temps_.acquire();
        loadValue(inst->getLhs(), frame, lhsReg, cache);
        out_ << "  sltu " << lhsReg << ", $zero, " << lhsReg << "\n";
        loadValue(inst->getRhs(), frame, rhsReg, cache);
        out_ << "  sltu " << rhsReg << ", $zero, " << rhsReg << "\n";
        auto dst = acquireTargetReg(inst, frame);
        if (inst->OpType() == LogicalOpType::AND) {
            out_ << "  and " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
        } else {
            out_ << "  or " << dst.name << ", " << lhsReg << ", " << rhsReg << "\n";
        }
        storeValue(inst, frame, dst.name, cache);
        temps_.release(lhsReg);
        temps_.release(rhsReg);
        releaseTarget(dst);
    }

    void emitZExt(const std::shared_ptr<ZExtInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        auto srcReg = temps_.acquire();
        loadValue(inst->getOperand(), frame, srcReg, cache);
        auto dst = acquireTargetReg(inst, frame);
        out_ << "  sltu " << dst.name << ", $zero, " << srcReg << "\n";
        storeValue(inst, frame, dst.name, cache);
        temps_.release(srcReg);
        releaseTarget(dst);
    }

    void emitUnary(const std::shared_ptr<UnaryOperator> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        auto opReg = temps_.acquire();
        loadValue(inst->getOperand(), frame, opReg, cache);
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
        storeValue(inst, frame, dst.name, cache);
        temps_.release(opReg);
        releaseTarget(dst);
    }

    void emitCall(const std::shared_ptr<CallInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        const auto funcName = sanitizeName(inst->getFunction()->getName());
        const auto &args = inst->getArgs();
        if (curInductionAddr_) {
            out_ << "  addi $sp, $sp, -4\n";
            out_ << "  sw $t7, 0($sp)\n";
        }
        const int argCount = static_cast<int>(args.size());
        for (int i = argCount - 1; i >= 4; --i) {
            auto arg = args[i];
            const bool isPointer = arg && arg->getType() && arg->getType()->is(Type::ArrayTyID);
            auto argReg = temps_.acquire();
            if (isPointer) {
                loadAddress(arg, frame, argReg);
            } else {
                loadValue(arg, frame, argReg, cache);
            }
            out_ << "  addi $sp, $sp, -4\n";
            out_ << "  sw " << argReg << ", 0($sp)\n";
            temps_.release(argReg);
        }
        for (int i = 0; i < argCount && i < 4; ++i) {
            auto arg = args[i];
            const bool isPointer = arg && arg->getType() && arg->getType()->is(Type::ArrayTyID);
            if (isPointer) {
                loadAddress(arg, frame, "$a" + std::to_string(i));
            } else {
                loadValue(arg, frame, "$a" + std::to_string(i), cache);
            }
        }
        out_ << "  jal " << funcName << "\n";
        emitNop();
        if (argCount > 4) {
            out_ << "  addi $sp, $sp, " << (argCount - 4) * 4 << "\n";
        }
        if (curInductionAddr_) {
            out_ << "  lw $t7, 0($sp)\n";
            out_ << "  addi $sp, $sp, 4\n";
        }
        cache.invalidateAll();
        const bool hasRet = inst->getType() && !inst->getType()->is(Type::VoidTyID);
        if (hasRet) {
            auto dst = acquireTargetReg(inst, frame);
            if (dst.name != "$v0") {
                out_ << "  move " << dst.name << ", $v0\n";
            }
            storeValue(inst, frame, dst.name, cache);
            releaseTarget(dst);
        }
    }

    void emitGEP(const std::shared_ptr<GetElementPtrInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
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
                loadValue(idx, frame, idxReg, cache);
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
        storeValue(inst, frame, dst.name, cache);
        releaseTarget(dst);
        temps_.release(baseReg);
    }

    void emitReturn(const std::shared_ptr<ReturnInst> &inst, const FrameInfo &frame, const std::string &retLabel,
        BlockRegCache &cache) {
        if (inst->getReturnValue()) {
            loadValue(inst->getReturnValue(), frame, "$v0", cache);
        }
        out_ << "  j " << retLabel << "\n";
        emitNop();
    }

    void emitJump(const std::shared_ptr<JumpInst> &inst, const FrameInfo &frame) {
        auto target = inst->getTarget();
        auto loopIt = loopByCond_.find(target.get());
        if (loopIt != loopByCond_.end() && loopForBlock_.count(curBlock_) == 0) {
            auto addr = loopIt->second->addr;
            auto offIt = frame.allocaOffsets.find(addr.get());
            if (offIt != frame.allocaOffsets.end()) {
                out_ << "  lw $t7, " << offIt->second << "($fp)\n";
            }
        }
        out_ << "  j " << frame.blockLabels.at(target.get()) << "\n";
        emitNop();
    }

    void emitBranch(const std::shared_ptr<BranchInst> &inst, const FrameInfo &frame, BlockRegCache &cache) {
        auto tLabel = frame.blockLabels.at(inst->getTrueBlock().get());
        auto fLabel = frame.blockLabels.at(inst->getFalseBlock().get());
        auto cond = inst->getCondition();
        if (cond && cond->getValueType() == ValueType::ConstantIntTy) {
            auto ci = std::static_pointer_cast<ConstantInt>(cond);
            out_ << "  j " << (ci->getValue() != 0 ? tLabel : fLabel) << "\n";
            emitNop();
            return;
        }
        auto cmp = std::dynamic_pointer_cast<CompareOperator>(cond);
        if (cmp) {
            emitBranchCompare(cmp, frame, cache, tLabel, fLabel);
            return;
        }
        auto condReg = temps_.acquire();
        loadValue(cond, frame, condReg, cache);
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
