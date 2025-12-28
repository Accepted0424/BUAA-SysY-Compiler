#include "llvm/include/ir/pass/PassManager.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "llvm/include/ir/type.h"
#include "llvm/include/ir/value/BasicBlock.h"
#include "llvm/include/ir/value/ConstantInt.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/Use.h"
#include "llvm/include/ir/value/inst/BinaryInstruction.h"
#include "llvm/include/ir/value/inst/Instruction.h"
#include "llvm/include/ir/value/inst/UnaryInstruction.h"

namespace {

ConstantIntPtr asConstInt(const ValuePtr &value) {
    return std::dynamic_pointer_cast<ConstantInt>(value);
}

bool isRemovableInst(const InstructionPtr &inst) {
    switch (inst->getValueType()) {
        case ValueType::AllocaInstTy:
        case ValueType::BinaryOperatorTy:
        case ValueType::CompareInstTy:
        case ValueType::LogicalInstTy:
        case ValueType::ZExtInstTy:
        case ValueType::UnaryOperatorTy:
        case ValueType::GetElementPtrInstTy:
        case ValueType::LoadInstTy:
            return true;
        default:
            return false;
    }
}

void dropInstructionUses(const InstructionPtr &inst) {
    for (const auto &op : inst->getOperands()) {
        if (op) {
            op->removeUse(inst.get());
        }
    }
}

void dropBlockUses(const BasicBlockPtr &bb) {
    for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
        dropInstructionUses(*instIt);
    }
}

InstructionPtr getTerminator(const BasicBlockPtr &bb) {
    InstructionPtr last = nullptr;
    for (auto it = bb->instructionBegin(); it != bb->instructionEnd(); ++it) {
        last = *it;
    }
    return last;
}

class ConstantFoldPass : public FunctionPass {
public:
    bool run(const FunctionPtr &func, Module &module) override {
        bool changed = false;
        auto ctx = module.getContext();
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            std::vector<InstructionPtr> toRemove;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                auto inst = *instIt;
                ValuePtr replacement = nullptr;

                if (inst->getValueType() == ValueType::BinaryOperatorTy) {
                    auto bin = std::static_pointer_cast<BinaryOperator>(inst);
                    auto lhs = bin->lhs_;
                    auto rhs = bin->rhs_;
                    auto lhsConst = asConstInt(lhs);
                    auto rhsConst = asConstInt(rhs);
                    if (lhsConst && rhsConst) {
                        const int l = lhsConst->getValue();
                        const int r = rhsConst->getValue();
                        bool ok = true;
                        int res = 0;
                        switch (bin->OpType()) {
                            case BinaryOpType::ADD: res = l + r; break;
                            case BinaryOpType::SUB: res = l - r; break;
                            case BinaryOpType::MUL: res = l * r; break;
                            case BinaryOpType::DIV:
                                if (r == 0) ok = false;
                                else res = l / r;
                                break;
                            case BinaryOpType::MOD:
                                if (r == 0) ok = false;
                                else res = l % r;
                                break;
                        }
                        if (ok) {
                            replacement = ConstantInt::create(inst->getType(), res);
                        }
                    } else {
                        if (bin->OpType() == BinaryOpType::ADD) {
                            if (rhsConst && rhsConst->getValue() == 0) replacement = lhs;
                            if (lhsConst && lhsConst->getValue() == 0) replacement = rhs;
                        } else if (bin->OpType() == BinaryOpType::SUB) {
                            if (rhsConst && rhsConst->getValue() == 0) replacement = lhs;
                        } else if (bin->OpType() == BinaryOpType::MUL) {
                            if ((rhsConst && rhsConst->getValue() == 0) ||
                                (lhsConst && lhsConst->getValue() == 0)) {
                                replacement = ConstantInt::create(inst->getType(), 0);
                            } else if (rhsConst && rhsConst->getValue() == 1) {
                                replacement = lhs;
                            } else if (lhsConst && lhsConst->getValue() == 1) {
                                replacement = rhs;
                            }
                        } else if (bin->OpType() == BinaryOpType::DIV) {
                            if (rhsConst && rhsConst->getValue() == 1) replacement = lhs;
                        } else if (bin->OpType() == BinaryOpType::MOD) {
                            if (rhsConst && rhsConst->getValue() == 1) {
                                replacement = ConstantInt::create(inst->getType(), 0);
                            }
                        }
                    }
                } else if (inst->getValueType() == ValueType::CompareInstTy) {
                    auto cmp = std::static_pointer_cast<CompareOperator>(inst);
                    auto lhsConst = asConstInt(cmp->getLhs());
                    auto rhsConst = asConstInt(cmp->getRhs());
                    if (lhsConst && rhsConst) {
                        const int l = lhsConst->getValue();
                        const int r = rhsConst->getValue();
                        bool res = false;
                        switch (cmp->OpType()) {
                            case CompareOpType::EQL: res = (l == r); break;
                            case CompareOpType::NEQ: res = (l != r); break;
                            case CompareOpType::LSS: res = (l < r); break;
                            case CompareOpType::GRE: res = (l > r); break;
                            case CompareOpType::LEQ: res = (l <= r); break;
                            case CompareOpType::GEQ: res = (l >= r); break;
                        }
                        replacement = ConstantInt::create(inst->getType(), res ? 1 : 0);
                    }
                } else if (inst->getValueType() == ValueType::LogicalInstTy) {
                    auto logic = std::static_pointer_cast<LogicalOperator>(inst);
                    auto lhsConst = asConstInt(logic->getLhs());
                    auto rhsConst = asConstInt(logic->getRhs());
                    if (lhsConst && rhsConst) {
                        const int l = lhsConst->getValue() != 0;
                        const int r = rhsConst->getValue() != 0;
                        int res = 0;
                        if (logic->OpType() == LogicalOpType::AND) {
                            res = (l && r) ? 1 : 0;
                        } else {
                            res = (l || r) ? 1 : 0;
                        }
                        replacement = ConstantInt::create(inst->getType(), res);
                    }
                } else if (inst->getValueType() == ValueType::UnaryOperatorTy) {
                    auto un = std::static_pointer_cast<UnaryOperator>(inst);
                    auto opConst = asConstInt(un->getOperand());
                    if (opConst) {
                        int v = opConst->getValue();
                        int res = v;
                        switch (un->OpType()) {
                            case UnaryOpType::POS: res = v; break;
                            case UnaryOpType::NEG: res = -v; break;
                            case UnaryOpType::NOT: res = (v == 0) ? 1 : 0; break;
                        }
                        replacement = ConstantInt::create(inst->getType(), res);
                    }
                } else if (inst->getValueType() == ValueType::ZExtInstTy) {
                    auto zext = std::static_pointer_cast<ZExtInst>(inst);
                    auto opConst = asConstInt(zext->getOperand());
                    if (opConst) {
                        int res = opConst->getValue() != 0 ? 1 : 0;
                        replacement = ConstantInt::create(inst->getType(), res);
                    }
                }

                if (replacement && replacement.get() != inst.get()) {
                    inst->replaceAllUsesWith(replacement);
                    toRemove.push_back(inst);
                    changed = true;
                }
            }

            for (const auto &inst : toRemove) {
                dropInstructionUses(inst);
                bb->removeInstruction(inst);
            }
        }
        return changed;
    }
};

class DcePass : public FunctionPass {
public:
    bool run(const FunctionPtr &func, Module &) override {
        bool changed = false;
        std::unordered_set<const Value*> deadAllocas;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                if ((*instIt)->getValueType() == ValueType::AllocaInstTy) {
                    deadAllocas.insert(instIt->get());
                }
            }
        }
        for (auto it = deadAllocas.begin(); it != deadAllocas.end(); ) {
            auto allocaVal = *it;
            bool live = false;
            for (const auto &use : allocaVal->getUses()) {
                auto *user = use->getUser();
                if (!user) continue;
                if (user->getValueType() == ValueType::StoreInstTy) {
                    auto st = static_cast<StoreInst*>(user);
                    if (st->getAddressOperand().get() == allocaVal) {
                        continue;
                    }
                }
                live = true;
                break;
            }
            if (live) {
                it = deadAllocas.erase(it);
            } else {
                ++it;
            }
        }

        if (!deadAllocas.empty()) {
            for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
                auto bb = *bbIt;
                for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ) {
                    auto inst = *instIt;
                    auto nextIt = instIt;
                    ++nextIt;
                    if (inst->getValueType() == ValueType::StoreInstTy) {
                        auto st = std::static_pointer_cast<StoreInst>(inst);
                        if (deadAllocas.count(st->getAddressOperand().get()) > 0) {
                            dropInstructionUses(inst);
                            bb->removeInstruction(inst);
                            changed = true;
                            instIt = nextIt;
                            continue;
                        }
                    }
                    instIt = nextIt;
                }
            }
            for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
                auto bb = *bbIt;
                for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ) {
                    auto inst = *instIt;
                    auto nextIt = instIt;
                    ++nextIt;
                    if (inst->getValueType() == ValueType::AllocaInstTy &&
                        deadAllocas.count(inst.get()) > 0) {
                        dropInstructionUses(inst);
                        bb->removeInstruction(inst);
                        changed = true;
                        instIt = nextIt;
                        continue;
                    }
                    instIt = nextIt;
                }
            }
        }

        std::vector<std::pair<InstructionPtr, BasicBlockPtr>> worklist;
        std::unordered_map<const Instruction*, BasicBlockPtr> defBlock;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
                auto inst = *instIt;
                defBlock[inst.get()] = bb;
                if (isRemovableInst(inst) && inst->getUseCount() == 0) {
                    worklist.emplace_back(inst, bb);
                }
            }
        }

        while (!worklist.empty()) {
            auto [inst, bb] = worklist.back();
            worklist.pop_back();
            if (!inst || !bb) continue;
            if (inst->getUseCount() != 0) continue;

            std::vector<ValuePtr> ops = inst->getOperands();
            dropInstructionUses(inst);
            bb->removeInstruction(inst);
            changed = true;

            for (const auto &op : ops) {
                auto opInst = std::dynamic_pointer_cast<Instruction>(op);
                if (opInst && isRemovableInst(opInst) && opInst->getUseCount() == 0) {
                    auto defIt = defBlock.find(opInst.get());
                    if (defIt != defBlock.end()) {
                        worklist.emplace_back(opInst, defIt->second);
                    }
                }
            }
        }
        return changed;
    }
};

class CfgSimplifyPass : public FunctionPass {
public:
    bool run(const FunctionPtr &func, Module &) override {
        bool changed = false;
        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            auto term = getTerminator(bb);
            if (!term || term->getValueType() != ValueType::BranchInstTy) continue;
            auto br = std::static_pointer_cast<BranchInst>(term);
            auto condConst = asConstInt(br->getCondition());
            if (!condConst) continue;
            const bool takeTrue = condConst->getValue() != 0;
            auto target = takeTrue ? br->getTrueBlock() : br->getFalseBlock();
            auto jump = JumpInst::create(target);
            dropInstructionUses(term);
            bb->removeInstruction(term);
            bb->insertInstruction(jump);
            changed = true;
        }

        std::unordered_set<BasicBlockPtr> reachable;
        std::queue<BasicBlockPtr> work;
        if (auto entry = func->getEntryBlock()) {
            reachable.insert(entry);
            work.push(entry);
        }
        while (!work.empty()) {
            auto bb = work.front();
            work.pop();
            auto term = getTerminator(bb);
            if (!term) continue;
            if (term->getValueType() == ValueType::JumpInstTy) {
                auto j = std::static_pointer_cast<JumpInst>(term);
                auto target = j->getTarget();
                if (target && reachable.insert(target).second) {
                    work.push(target);
                }
            } else if (term->getValueType() == ValueType::BranchInstTy) {
                auto br = std::static_pointer_cast<BranchInst>(term);
                auto t = br->getTrueBlock();
                auto f = br->getFalseBlock();
                if (t && reachable.insert(t).second) work.push(t);
                if (f && reachable.insert(f).second) work.push(f);
            }
        }

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ) {
            auto bb = *bbIt;
            auto nextIt = bbIt;
            ++nextIt;
            if (reachable.count(bb) == 0) {
                dropBlockUses(bb);
                func->removeBasicBlock(bb);
                changed = true;
            }
            bbIt = nextIt;
        }

        bool merged = true;
        while (merged) {
            merged = false;
            for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
                auto bb = *bbIt;
                if (bb == func->getEntryBlock()) continue;
                if (bb->InstructionCount() != 1) continue;
                auto term = getTerminator(bb);
                if (!term || term->getValueType() != ValueType::JumpInstTy) continue;
                auto j = std::static_pointer_cast<JumpInst>(term);
                auto target = j->getTarget();
                if (!target || target == bb) continue;

                for (auto predIt = func->basicBlockBegin(); predIt != func->basicBlockEnd(); ++predIt) {
                    auto pred = *predIt;
                    auto predTerm = getTerminator(pred);
                    if (!predTerm) continue;
                    if (predTerm->getValueType() == ValueType::JumpInstTy) {
                        auto pj = std::static_pointer_cast<JumpInst>(predTerm);
                        if (pj->getTarget() == bb) {
                            pj->setTarget(target);
                        }
                    } else if (predTerm->getValueType() == ValueType::BranchInstTy) {
                        auto pb = std::static_pointer_cast<BranchInst>(predTerm);
                        if (pb->getTrueBlock() == bb) {
                            pb->setTrueBlock(target);
                        }
                        if (pb->getFalseBlock() == bb) {
                            pb->setFalseBlock(target);
                        }
                    }
                }

                dropBlockUses(bb);
                func->removeBasicBlock(bb);
                merged = true;
                changed = true;
                break;
            }
        }

        return changed;
    }
};

}  // namespace

void PassManager::run(Module &module) {
    for (auto it = module.functionBegin(); it != module.functionEnd(); ++it) {
        auto func = *it;
        bool changed = false;
        do {
            changed = false;
            for (auto &pass : passes_) {
                if (pass->run(func, module)) {
                    changed = true;
                }
            }
        } while (changed);
    }
}

void addDefaultPasses(PassManager &pm) {
    pm.addPass(std::make_unique<ConstantFoldPass>());
    pm.addPass(std::make_unique<DcePass>());
    pm.addPass(std::make_unique<CfgSimplifyPass>());
}
