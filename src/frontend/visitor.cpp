#include "visitor.h"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logger.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/type.h"
#include "llvm/include/ir/value/Argument.h"
#include "llvm/include/ir/value/ConstantArray.h"
#include "llvm/include/ir/value/ConstantInt.h"
#include "llvm/include/ir/value/GlobalVariable.h"
#include "llvm/include/ir/value/inst/BinaryInstruction.h"
#include "llvm/include/ir/value/inst/UnaryInstruction.h"
#include "llvm/include/ir/value/inst/Instruction.h"

namespace {
ConstantIntPtr makeConst(LlvmContext* ctx, int value) {
    return ConstantInt::create(ctx->getIntegerTy(), value);
}

ConstantIntPtr asConstInt(const ValuePtr &value) {
    return std::dynamic_pointer_cast<ConstantInt>(value);
}

bool isConstValue(const ValuePtr &value, int expected) {
    auto ci = asConstInt(value);
    return ci && ci->getValue() == expected;
}

std::string ptrKey(const ValuePtr &value) {
    return std::to_string(reinterpret_cast<uintptr_t>(value.get()));
}

std::string typeKey(const TypePtr &type) {
    return std::to_string(reinterpret_cast<uintptr_t>(type.get()));
}

bool isCommutative(const BinaryOpType op) {
    return op == BinaryOpType::ADD || op == BinaryOpType::MUL;
}

bool isCommutative(const CompareOpType op) {
    return op == CompareOpType::EQL || op == CompareOpType::NEQ;
}

bool isRemovable(const InstructionPtr &inst) {
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
}

void Visitor::insertInst(const InstructionPtr &inst, bool toEntry) {
    if (toEntry && entry_block_ != nullptr) {
        // Keep allocas (and other entry-only setup) before the first non-alloca,
        // so terminators added later stay at the end of the block.
        auto it = entry_block_->instructionBegin();
        for (; it != entry_block_->instructionEnd(); ++it) {
            if ((*it)->getValueType() != ValueType::AllocaInstTy) {
                break;
            }
        }
        entry_block_->insertInstruction(it, inst);
        if (inst && inst->getValueType() == ValueType::StoreInstTy) {
            auto st = std::static_pointer_cast<StoreInst>(inst);
            invalidateLoadCache(st->getAddressOperand());
        } else if (inst && inst->getValueType() == ValueType::CallInstTy) {
            clearLoadCache();
        }
        return;
    }
    if (cur_block_ != nullptr) {
        cur_block_->insertInstruction(inst);
        if (inst && inst->getValueType() == ValueType::StoreInstTy) {
            auto st = std::static_pointer_cast<StoreInst>(inst);
            invalidateLoadCache(st->getAddressOperand());
        } else if (inst && inst->getValueType() == ValueType::CallInstTy) {
            clearLoadCache();
        }
    }
}

std::unordered_map<std::string, ValuePtr> &Visitor::currentCseTable() {
    static std::unordered_map<std::string, ValuePtr> dummy;
    if (cur_block_ == nullptr) return dummy;
    return cseTables_[cur_block_.get()];
}

Visitor::CseResult Visitor::reuseBinary(BinaryOpType op, ValuePtr lhs, ValuePtr rhs) {
    auto inst = BinaryOperator::create(op, lhs, rhs);
    if (cur_block_ == nullptr) {
        return {inst, true};
    }
    auto a = ptrKey(lhs);
    auto b = ptrKey(rhs);
    if (isCommutative(op) && a > b) {
        std::swap(a, b);
    }
    const std::string key = "bin|" + std::to_string(static_cast<int>(op)) + "|" + a + "|" + b;
    auto &table = currentCseTable();
    auto it = table.find(key);
    if (it != table.end()) {
        return {it->second, false};
    }
    table[key] = inst;
    return {inst, true};
}

Visitor::CseResult Visitor::reuseCompare(CompareOpType op, ValuePtr lhs, ValuePtr rhs) {
    auto inst = CompareOperator::create(op, lhs, rhs);
    if (cur_block_ == nullptr) {
        return {inst, true};
    }
    auto a = ptrKey(lhs);
    auto b = ptrKey(rhs);
    if (isCommutative(op) && a > b) {
        std::swap(a, b);
    }
    const std::string key = "cmp|" + std::to_string(static_cast<int>(op)) + "|" + a + "|" + b;
    auto &table = currentCseTable();
    auto it = table.find(key);
    if (it != table.end()) {
        return {it->second, false};
    }
    table[key] = inst;
    return {inst, true};
}

Visitor::CseResult Visitor::reuseUnary(UnaryOpType op, ValuePtr operand) {
    auto inst = UnaryOperator::create(op, operand);
    if (cur_block_ == nullptr) {
        return {inst, true};
    }
    const std::string key = "un|" + std::to_string(static_cast<int>(op)) + "|" + ptrKey(operand);
    auto &table = currentCseTable();
    auto it = table.find(key);
    if (it != table.end()) {
        return {it->second, false};
    }
    table[key] = inst;
    return {inst, true};
}

Visitor::CseResult Visitor::reuseZExt(TypePtr targetType, ValuePtr operand) {
    auto inst = ZExtInst::create(targetType, operand);
    if (cur_block_ == nullptr) {
        return {inst, true};
    }
    const std::string key = "zext|" + typeKey(targetType) + "|" + ptrKey(operand);
    auto &table = currentCseTable();
    auto it = table.find(key);
    if (it != table.end()) {
        return {it->second, false};
    }
    table[key] = inst;
    return {inst, true};
}

Visitor::CseResult Visitor::reuseGEP(TypePtr elementType, ValuePtr address, const std::vector<ValuePtr> &indices) {
    auto inst = GetElementPtrInst::create(elementType, address, indices);
    if (cur_block_ == nullptr) {
        return {inst, true};
    }
    std::string key = "gep|" + typeKey(elementType) + "|" + ptrKey(address);
    key += "|" + std::to_string(indices.size());
    for (const auto &idx : indices) {
        key += "|" + ptrKey(idx);
    }
    auto &table = currentCseTable();
    auto it = table.find(key);
    if (it != table.end()) {
        return {it->second, false};
    }
    table[key] = inst;
    return {inst, true};
}

void Visitor::runDCE(const FunctionPtr &func) {
    if (!func) return;
    // Remove stores to local allocas that are never loaded (dead temporaries).
    std::unordered_set<const Value*> deadAllocas;
    for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
        auto bb = *bbIt;
        for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
            auto inst = *instIt;
            if (inst->getValueType() == ValueType::AllocaInstTy) {
                deadAllocas.insert(inst.get());
            } else if (inst->getValueType() == ValueType::LoadInstTy) {
                auto ld = std::static_pointer_cast<LoadInst>(inst);
                deadAllocas.erase(ld->getAddressOperand().get());
            } else if (inst->getValueType() == ValueType::GetElementPtrInstTy) {
                auto gep = std::static_pointer_cast<GetElementPtrInst>(inst);
                deadAllocas.erase(gep->getAddressOperand().get());
            }
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
                        bb->removeInstruction(inst);
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
                    bb->removeInstruction(inst);
                    instIt = nextIt;
                    continue;
                }
                instIt = nextIt;
            }
        }
    }
    std::unordered_map<const Value*, int> useCount;
    std::unordered_map<const Value*, BasicBlockPtr> defBlock;
    std::vector<std::pair<InstructionPtr, BasicBlockPtr>> defs;

    for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
        auto bb = *bbIt;
        for (auto instIt = bb->instructionBegin(); instIt != bb->instructionEnd(); ++instIt) {
            auto inst = *instIt;
            defs.emplace_back(inst, bb);
            defBlock[inst.get()] = bb;
            std::vector<ValuePtr> ops;
            collectOperands(inst, ops);
            for (const auto &op : ops) {
                if (op) {
                    ++useCount[op.get()];
                }
            }
        }
    }

    std::vector<std::pair<InstructionPtr, BasicBlockPtr>> worklist;
    for (const auto &[inst, bb] : defs) {
        if (isRemovable(inst) && useCount[inst.get()] == 0) {
            worklist.emplace_back(inst, bb);
        }
    }

    while (!worklist.empty()) {
        auto [inst, bb] = worklist.back();
        worklist.pop_back();
        if (!inst || !bb) continue;

        std::vector<ValuePtr> ops;
        collectOperands(inst, ops);
        bb->removeInstruction(inst);

        for (const auto &op : ops) {
            if (!op) continue;
            auto it = useCount.find(op.get());
            if (it == useCount.end() || it->second <= 0) continue;
            --(it->second);
            auto opInst = std::dynamic_pointer_cast<Instruction>(op);
            if (opInst && isRemovable(opInst) && it->second == 0) {
                auto defIt = defBlock.find(op.get());
                if (defIt != defBlock.end()) {
                    worklist.emplace_back(opInst, defIt->second);
                }
            }
        }
    }
}

void Visitor::invalidateLoadCache(const ValuePtr &address) {
    if (!cur_block_ || !address) return;
    auto &cache = loadCaches_[cur_block_.get()];
    cache.erase(address.get());
}

void Visitor::clearLoadCache() {
    if (!cur_block_) return;
    loadCaches_[cur_block_.get()].clear();
}

BasicBlockPtr Visitor::newBlock(const std::string &hint) {
    auto bb = BasicBlock::create(cur_func_);
    std::string name = hint;
    if (!name.empty()) {
        name += "." + std::to_string(blockId_++);
    } else {
        name = "bb." + std::to_string(blockId_++);
    }
    bb->setName(name);
    return bb;
}

ValuePtr Visitor::loadIfPointer(const ValuePtr &value) {
    if (value == nullptr) {
        return nullptr;
    }
    if (value->getType() && value->getType()->is(Type::ArrayTyID) &&
        value->getValueType() != ValueType::GetElementPtrInstTy) {
        return value;
    }
    switch (value->getValueType()) {
        case ValueType::AllocaInstTy:
        case ValueType::GlobalVariableTy:
        case ValueType::GetElementPtrInstTy: {
            if (cur_block_ != nullptr) {
                auto &cache = loadCaches_[cur_block_.get()];
                auto it = cache.find(value.get());
                if (it != cache.end()) {
                    return it->second;
                }
            }
            auto load = LoadInst::create(ir_module_.getContext()->getIntegerTy(), value);
            insertInst(load);
            if (cur_block_ != nullptr) {
                loadCaches_[cur_block_.get()][value.get()] = load;
            }
            return load;
        }
        default:
            return value;
    }
}

ValuePtr Visitor::toBool(const ValuePtr &value) {
    // Values coming from compares or logical ops are already boolean-like; reuse them.
    if (value && (value->getValueType() == ValueType::CompareInstTy ||
                  value->getValueType() == ValueType::LogicalInstTy)) {
        return value;
    }
    if (auto ci = asConstInt(value)) {
        return ConstantInt::create(ir_module_.getContext()->getBoolTy(), ci->getValue() != 0);
    }
    auto ctx = ir_module_.getContext();
    auto rhs = makeConst(ctx, 0);
    return createCmp(CompareOpType::NEQ, value, rhs);
}

ValuePtr Visitor::zextToInt32(const ValuePtr &value) {
    if (!value || !value->getType()) {
        return value;
    }
    auto ctxInt32 = ir_module_.getContext()->getIntegerTy();
    const bool forceZext = value->getValueType() == ValueType::CompareInstTy ||
                           value->getValueType() == ValueType::LogicalInstTy;
    if (value->getType()->is(Type::IntegerTyID)) {
        auto intType = std::static_pointer_cast<IntegerType>(value->getType());
        if (!forceZext && intType->getBitWidth() == 32) {
            return value;
        }
        if (auto ci = asConstInt(value)) {
            return ConstantInt::create(ctxInt32, ci->getValue());
        }
        auto res = reuseZExt(ctxInt32, value);
        if (res.created) {
            insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
        }
        return res.value;
    }
    return value;
}

ValuePtr Visitor::createCmp(CompareOpType op, ValuePtr lhs, ValuePtr rhs) {
    lhs = loadIfPointer(lhs);
    rhs = loadIfPointer(rhs);
    lhs = zextToInt32(lhs);
    rhs = zextToInt32(rhs);
    if (auto lhsConst = asConstInt(lhs)) {
        if (auto rhsConst = asConstInt(rhs)) {
            const int l = lhsConst->getValue();
            const int r = rhsConst->getValue();
            bool res = false;
            switch (op) {
                case CompareOpType::EQL: res = (l == r); break;
                case CompareOpType::NEQ: res = (l != r); break;
                case CompareOpType::LSS: res = (l < r); break;
                case CompareOpType::GRE: res = (l > r); break;
                case CompareOpType::LEQ: res = (l <= r); break;
                case CompareOpType::GEQ: res = (l >= r); break;
            }
            return ConstantInt::create(ir_module_.getContext()->getBoolTy(), res ? 1 : 0);
        }
    }
    auto res = reuseCompare(op, lhs, rhs);
    if (res.created) {
        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
    }
    return res.value;
}

std::optional<int> Visitor::constValueOfLVal(const LVal &lval) {
    if (!cur_scope_ || !cur_scope_->existInSymTable(lval.ident->content)) {
        return std::nullopt;
    }
    auto sym = cur_scope_->getSymbol(lval.ident->content);
    if (sym->type != CONST_INT && sym->type != CONST_INT_ARRAY) {
        return std::nullopt;
    }
    if (sym->type == CONST_INT) {
        if (auto ci = std::dynamic_pointer_cast<ConstantInt>(sym->value)) {
            return ci->getValue();
        }
        if (auto gv = std::dynamic_pointer_cast<GlobalVariable>(sym->value)) {
            auto gvVal = std::dynamic_pointer_cast<ConstantInt>(gv->value_);
            if (gvVal) {
                return gvVal->getValue();
            }
        }
        return std::nullopt;
    }

    // CONST_INT_ARRAY
    if (lval.index == nullptr) {
        return std::nullopt;
    }
    auto idxVal = evalConstExpValue(*lval.index);
    if (!idxVal.has_value() || *idxVal < 0) {
        return std::nullopt;
    }
    if (auto gv = std::dynamic_pointer_cast<GlobalVariable>(sym->value)) {
        auto constArr = std::dynamic_pointer_cast<ConstantArray>(gv->value_);
        if (constArr) {
            const auto &elems = constArr->getElements();
            if (*idxVal < static_cast<int>(elems.size())) {
                return elems[*idxVal]->getValue();
            }
        }
    }
    return std::nullopt;
}

std::optional<int> Visitor::evalConstAddWithLVal(const AddExp &root) {
    std::function<std::optional<int>(const AddExp&)> evalAdd;
    std::function<std::optional<int>(const MulExp&)> evalMul;
    std::function<std::optional<int>(const UnaryExp&)> evalUnary;
    std::function<std::optional<int>(const PrimaryExp&)> evalPrimary;

    evalPrimary = [&](const PrimaryExp &p) -> std::optional<int> {
        switch (p.kind) {
            case PrimaryExp::NUMBER:
                return std::stoi(p.number->value);
            case PrimaryExp::EXP:
                return evalAdd(*p.exp->addExp);
            case PrimaryExp::LVAL:
            default:
                return p.lval ? constValueOfLVal(*p.lval) : std::nullopt;
        }
    };

    evalUnary = [&](const UnaryExp &u) -> std::optional<int> {
        if (u.kind == UnaryExp::PRIMARY) return evalPrimary(*u.primary);
        if (u.kind == UnaryExp::UNARY_OP) {
            auto val = evalUnary(*u.unary->expr);
            if (!val.has_value()) return std::nullopt;
            switch (u.unary->op->kind) {
                case UnaryOp::PLUS: return *val;
                case UnaryOp::MINU: return -*val;
                case UnaryOp::NOT: return !*val;
            }
        }
        return std::nullopt;
    };

    evalMul = [&](const MulExp &m) -> std::optional<int> {
        auto res = evalUnary(*m.first);
        if (!res.has_value()) return std::nullopt;
        for (const auto &[op, rhs] : m.rest) {
            auto rhsVal = evalUnary(*rhs);
            if (!rhsVal.has_value()) return std::nullopt;
            switch (op) {
                case MulExp::MULT: res = *res * *rhsVal; break;
                case MulExp::DIV: res = *res / *rhsVal; break;
                case MulExp::MOD: res = *res % *rhsVal; break;
            }
        }
        return res;
    };

    evalAdd = [&](const AddExp &a) -> std::optional<int> {
        auto res = evalMul(*a.first);
        if (!res.has_value()) return std::nullopt;
        for (const auto &[op, rhs] : a.rest) {
            auto rhsVal = evalMul(*rhs);
            if (!rhsVal.has_value()) return std::nullopt;
            if (op == AddExp::PLUS) res = *res + *rhsVal;
            else res = *res - *rhsVal;
        }
        return res;
    };

    return evalAdd(root);
}

std::optional<int> Visitor::evalConstExpValue(const Exp &exp) {
    return evalConstAddWithLVal(*exp.addExp);
}

std::optional<int> Visitor::evalConstConstExp(const ConstExp &constExp) {
    return evalConstAddWithLVal(*constExp.addExp);
}

ValuePtr Visitor::getLValAddress(const LVal &lval) {
    const std::string &name = lval.ident->content;
    if (!cur_scope_->existInSymTable(name)) {
        ErrorReporter::error(lval.lineno, ERR_UNDEFINED_NAME);
        return nullptr;
    }
    auto symbol = cur_scope_->getSymbol(name);
    auto base = symbol->value;
    if (lval.index != nullptr) {
        auto idxVal = loadIfPointer(visitExp(*lval.index));
        if (idxVal == nullptr) {
            return nullptr;
        }
        auto ctx = ir_module_.getContext();
        std::vector<ValuePtr> indices;
        auto baseType = base->getType();
        TypePtr gepType = baseType;
        if (baseType && baseType->is(Type::ArrayTyID)) {
            auto arrType = std::static_pointer_cast<ArrayType>(baseType);
            if (arrType->getElementNum() >= 0) {
                indices.push_back(makeConst(ctx, 0));
            }
            gepType = arrType->getElementType();
        }
        indices.push_back(idxVal);
        auto res = reuseGEP(gepType, base, indices);
        if (res.created) {
            insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
        }
        base = res.value;
    }
    return base;
}

ValuePtr Visitor::visitPrimaryExp(const PrimaryExp &primaryExp) {
    switch (primaryExp.kind) {
        case PrimaryExp::EXP:
            return visitExp(*primaryExp.exp);

        case PrimaryExp::LVAL: {
            if (primaryExp.lval) {
                if (auto constVal = constValueOfLVal(*primaryExp.lval)) {
                    return ConstantInt::create(ir_module_.getContext()->getIntegerTy(), *constVal);
                }
            }
            auto addr = getLValAddress(*primaryExp.lval);
            return loadIfPointer(addr);
        }

        case PrimaryExp::NUMBER: {
            auto context = ir_module_.getContext();
            const int val = std::stoi(primaryExp.number->value);
            return ConstantInt::create(context->getIntegerTy(), val);
        }

        default:
            LOG_ERROR("Unreachable in Visitor::visitPrimaryExp");
            return nullptr;
    }
}

ValuePtr Visitor::visitUnaryExp(const UnaryExp &unaryExp) {
    switch (unaryExp.kind) {
        case UnaryExp::PRIMARY:
            return visitPrimaryExp(*unaryExp.primary);

        case UnaryExp::CALL: {
            auto symbol = cur_scope_->getFuncSymbol(unaryExp.call->ident->content);
            if (symbol == nullptr) {
                ErrorReporter::error(unaryExp.lineno, ERR_UNDEFINED_NAME);
                return nullptr;
            }
            std::vector<std::shared_ptr<Value>> args;

            const size_t providedCnt = unaryExp.call->params ? unaryExp.call->params->params.size() : 0;
            if (providedCnt != static_cast<size_t>(symbol->getParamCnt())) {
                ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_COUNT_MISMATCH);
            }

            if (unaryExp.call->params) {
                for (size_t i = 0; i < unaryExp.call->params->params.size(); ++i) {
                    auto paramType = symbol->params[i];
                    auto argVal = visitExp(*unaryExp.call->params->params[i]);
                    if (argVal == nullptr) {
                        ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_TYPE_MISMATCH);
                        args.push_back(argVal);
                        continue;
                    }

                    if (paramType->is(Type::ArrayTyID)) {
                        if (argVal && argVal->getType() && argVal->getType()->is(Type::ArrayTyID)) {
                            auto argArrayType = std::static_pointer_cast<ArrayType>(argVal->getType());
                            auto paramArrayType = std::static_pointer_cast<ArrayType>(paramType);
                            if (argArrayType->getElementNum() >= 0 && paramArrayType->getElementNum() < 0) {
                                auto zero = makeConst(ir_module_.getContext(), 0);
                                auto decay = reuseGEP(paramType, argVal, {zero, zero});
                                if (decay.created) {
                                    insertInst(std::dynamic_pointer_cast<Instruction>(decay.value));
                                }
                                argVal = decay.value;
                            }
                            if (argVal->getType() != paramType) {
                                ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_TYPE_MISMATCH);
                            }
                        } else {
                            ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_TYPE_MISMATCH);
                        }
                        args.push_back(argVal);
                    } else {
                        auto expValue = loadIfPointer(argVal);
                        if (expValue == nullptr ||
                            paramType != expValue->getType() ||
                            (expValue->getValueType() == ValueType::ConstantArrayTy)) {
                            ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_TYPE_MISMATCH);
                        }
                        args.push_back(expValue);
                    }
                }
            }
            auto call = CallInst::create(std::dynamic_pointer_cast<Function>(symbol->value), args);
            insertInst(call);
            return call;

        }

        case UnaryExp::UNARY_OP: {
            auto rhs = loadIfPointer(visitUnaryExp(*unaryExp.unary->expr));
            ValuePtr res = nullptr;
            switch (unaryExp.unary->op->kind) {
                case UnaryOp::PLUS:
                    res = rhs;
                    break;
                case UnaryOp::MINU:
                    {
                        if (auto ci = asConstInt(rhs)) {
                            res = ConstantInt::create(ir_module_.getContext()->getIntegerTy(), -ci->getValue());
                        } else {
                            auto uni = reuseUnary(UnaryOpType::NEG, rhs);
                            if (uni.created) {
                                insertInst(std::dynamic_pointer_cast<Instruction>(uni.value));
                            }
                            res = uni.value;
                        }
                    }
                    break;
                case UnaryOp::NOT:
                    if (auto ci = asConstInt(rhs)) {
                        res = ConstantInt::create(ir_module_.getContext()->getBoolTy(), ci->getValue() == 0);
                    } else {
                        res = createCmp(CompareOpType::EQL, rhs, makeConst(ir_module_.getContext(), 0));
                    }
                    break;
                default:
                    LOG_ERROR("Unreachable in Visitor::visitUnaryExp");
                    return nullptr;
            }
            return res;
        }
    }
    return nullptr;
}

ValuePtr Visitor::visitMulExp(const MulExp &mulExp) {
    auto lhs = loadIfPointer(visitUnaryExp(*mulExp.first));
    for (const auto &[op, rhs] : mulExp.rest) {
        auto rhsVal = loadIfPointer(visitUnaryExp(*rhs));
        if (auto lhsConst = asConstInt(lhs)) {
            if (auto rhsConst = asConstInt(rhsVal)) {
                const int l = lhsConst->getValue();
                const int r = rhsConst->getValue();
                bool folded = true;
                int resVal = 0;
                switch (op) {
                    case MulExp::MULT: resVal = l * r; break;
                    case MulExp::DIV:
                        if (r == 0) folded = false;
                        else resVal = l / r;
                        break;
                    case MulExp::MOD:
                        if (r == 0) folded = false;
                        else resVal = l % r;
                        break;
                }
                if (folded) {
                    lhs = ConstantInt::create(ir_module_.getContext()->getIntegerTy(), resVal);
                    continue;
                }
            }
        }
        switch (op) {
            case MulExp::MULT:
                {
                    if (isConstValue(lhs, 0) || isConstValue(rhsVal, 0)) {
                        lhs = makeConst(ir_module_.getContext(), 0);
                        break;
                    }
                    if (isConstValue(lhs, 1)) {
                        lhs = rhsVal;
                        break;
                    }
                    if (isConstValue(rhsVal, 1)) {
                        break;
                    }
                    auto res = reuseBinary(BinaryOpType::MUL, lhs, rhsVal);
                    if (res.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
                    }
                    lhs = res.value;
                }
                break;
            case MulExp::DIV:
                {
                    if (isConstValue(rhsVal, 1)) {
                        break;
                    }
                    auto res = reuseBinary(BinaryOpType::DIV, lhs, rhsVal);
                    if (res.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
                    }
                    lhs = res.value;
                }
                break;
            case MulExp::MOD:
                {
                    if (isConstValue(rhsVal, 1)) {
                        lhs = makeConst(ir_module_.getContext(), 0);
                        break;
                    }
                    auto res = reuseBinary(BinaryOpType::MOD, lhs, rhsVal);
                    if (res.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
                    }
                    lhs = res.value;
                }
                break;
        }
    }
    return lhs;
}

ValuePtr Visitor::visitAddExp(const AddExp &addExp) {
    auto lhs = loadIfPointer(visitMulExp(*addExp.first));
    for (const auto &[op, rhs] : addExp.rest) {
        auto rhsVal = loadIfPointer(visitMulExp(*rhs));
        if (auto lhsConst = asConstInt(lhs)) {
            if (auto rhsConst = asConstInt(rhsVal)) {
                const int l = lhsConst->getValue();
                const int r = rhsConst->getValue();
                const int resVal = (op == AddExp::PLUS) ? (l + r) : (l - r);
                lhs = ConstantInt::create(ir_module_.getContext()->getIntegerTy(), resVal);
                continue;
            }
        }
        switch (op) {
            case AddExp::PLUS:
                {
                    if (isConstValue(rhsVal, 0)) {
                        break;
                    }
                    if (isConstValue(lhs, 0)) {
                        lhs = rhsVal;
                        break;
                    }
                    auto res = reuseBinary(BinaryOpType::ADD, lhs, rhsVal);
                    if (res.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
                    }
                    lhs = res.value;
                }
                break;
            case AddExp::MINU:
                {
                    if (isConstValue(rhsVal, 0)) {
                        break;
                    }
                    auto res = reuseBinary(BinaryOpType::SUB, lhs, rhsVal);
                    if (res.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(res.value));
                    }
                    lhs = res.value;
                }
                break;
        }
    }
    return lhs;
}

ConstantIntPtr Visitor::visitConstExp(const ConstExp &constExp) {
    if (auto valOpt = evalConstConstExp(constExp)) {
        return ConstantInt::create(ir_module_.getContext()->getIntegerTy(), *valOpt);
    }
    LOG_ERROR("Unreachable in Visitor::visitConstExp");
    return nullptr;
}

ValuePtr Visitor::visitExp(const Exp &exp) {
    return visitAddExp(*exp.addExp);
}

void Visitor::visitConstDecl(const ConstDecl &constDecl) {
    auto context = ir_module_.getContext();

    for (const auto &constDef: constDecl.constDefs) {
        const std::string &name = constDef->ident->content;
        const int lineno = constDef->lineno;

        if (constDef->constExp == nullptr) {
            // scalar constant
            auto symbol = std::make_shared<ConstIntSymbol>(name, nullptr, lineno);
            if (constDef->constInitVal->kind == ConstInitVal::EXP) {
                auto val = visitConstExp(*constDef->constInitVal->exp);
                if (!cur_scope_->isGlobalScope()) {
                    // local: keep as immediate constant
                    symbol->value = val;
                } else {
                    // global
                    auto globalVar = GlobalVariable::create(context->getIntegerTy(), name, val, true);
                    symbol->value = globalVar;
                    ir_module_.addGlobalVar(globalVar);
                }
            }
            cur_scope_->addSymbol(symbol);
        } else {
            // constant array
            auto symbol = std::make_shared<ConstIntArraySymbol>(name, nullptr, lineno);
            const int arraySize = visitConstExp(*constDef->constExp)->getValue();
            auto arrayType = context->getArrayTy(context->getIntegerTy(), arraySize);
            auto initialValList = std::vector<ConstantIntPtr>{};
            for (const auto &constExp : constDef->constInitVal->list) {
                initialValList.push_back(visitConstExp(*constExp));
            }
            if (static_cast<int>(initialValList.size()) > arraySize) {
                initialValList.resize(arraySize);
            }
            while (static_cast<int>(initialValList.size()) < arraySize) {
                initialValList.push_back(makeConst(context, 0));
            }

            if (!cur_scope_->isGlobalScope()) {
                // local
                auto alloca = AllocaInst::create(arrayType, name);
                insertInst(alloca, true);
                for (int i = 0; i < static_cast<int>(initialValList.size()); i++) {
                    auto idxConst = makeConst(context, i);
                    auto gep = reuseGEP(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                    if (gep.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(gep.value));
                    }
                    auto store = StoreInst::create(initialValList[i], gep.value);
                    insertInst(store);
                }
                symbol->value = alloca;
            } else {
                auto constArray = ConstantArray::create(arrayType, initialValList);
                auto globalVar = GlobalVariable::create(arrayType, name, constArray, true);
                symbol->value = globalVar;
                ir_module_.addGlobalVar(globalVar);
            }
            cur_scope_->addSymbol(symbol);
        }
    }
}

void Visitor::visitVarDecl(const VarDecl &varDecl) {
    const bool isStatic = (varDecl.prefix == "static");

    auto context = ir_module_.getContext();

    for (const auto &varDef: varDecl.varDefs) {
        const std::string &name = varDef->ident->content;
        const int lineno = varDef->lineno;
        if (cur_scope_->existInScope(name)) {
            ErrorReporter::error(lineno, ERR_REDEFINED_NAME);
            continue;
        }
        std::shared_ptr<Symbol> symbol = nullptr;
        const bool isArray = (varDef->constExp != nullptr);
        const int arraySize = isArray && varDef->constExp ? visitConstExp(*varDef->constExp)->getValue() : 0;
        const bool isStaticLocal = isStatic && !cur_scope_->isGlobalScope();
        const std::string storageName = isStaticLocal
            ? (cur_func_->getName() + ".static." + name + "." + std::to_string(staticLocalId_++))
            : name;

        if (isStatic || cur_scope_->isGlobalScope()) {
            if (isArray) {
                auto type = context->getArrayTy(context->getIntegerTy(), arraySize);
                std::vector<ConstantIntPtr> initList;
                if (varDef->initVal && varDef->initVal->kind == InitVal::LIST) {
                    for (const auto &exp : varDef->initVal->list) {
                        auto val = visitExp(*exp);
                        auto ci = std::dynamic_pointer_cast<ConstantInt>(val);
                        if (!ci) {
                            if (auto evaluated = evalConstExpValue(*exp)) {
                                ci = makeConst(context, *evaluated);
                            }
                        }
                        if (ci) {
                            initList.push_back(ci);
                        }
                    }
                }
                if (static_cast<int>(initList.size()) > arraySize) {
                    initList.resize(arraySize);
                }
                while (static_cast<int>(initList.size()) < arraySize) {
                    initList.push_back(makeConst(context, 0));
                }
                auto initVal = ConstantArray::create(type, initList);
                auto gv = GlobalVariable::create(type, storageName, initVal, false);

                if (isStatic) {
                    symbol = std::make_shared<StaticIntArraySymbol>(name, gv, lineno);
                } else {
                    symbol = std::make_shared<IntArraySymbol>(name, gv, lineno);
                }

                ir_module_.addGlobalVar(gv);
            } else {
                ValuePtr initVal = nullptr;
                if (varDef->initVal && varDef->initVal->kind == InitVal::EXP) {
                    auto val = visitExp(*varDef->initVal->exp);
                    auto ci = std::dynamic_pointer_cast<ConstantInt>(val);
                    if (!ci) {
                        if (auto evaluated = evalConstExpValue(*varDef->initVal->exp)) {
                            ci = makeConst(context, *evaluated);
                        }
                    }
                    if (ci) {
                        initVal = ci;
                    }
                }
                auto gv = GlobalVariable::create(context->getIntegerTy(), storageName, initVal, false);

                if (isStatic) {
                    symbol = std::make_shared<StaticIntSymbol>(name, gv, lineno);
                } else {
                    symbol = std::make_shared<IntSymbol>(name, gv, lineno);
                }

                ir_module_.addGlobalVar(gv);
            }
        } else {
            // local variables
            if (isArray) {
                auto type = context->getArrayTy(context->getIntegerTy(), arraySize);
                auto alloca = AllocaInst::create(type, name);
                insertInst(alloca, true);
                symbol = std::make_shared<IntArraySymbol>(name, alloca, lineno);
                if (varDef->initVal && varDef->initVal->kind == InitVal::LIST) {
                    int idx = 0;
                    for (const auto &exp : varDef->initVal->list) {
                        if (idx >= arraySize && arraySize > 0) break;
                        auto val = loadIfPointer(visitExp(*exp));
                        auto idxConst = makeConst(context, idx++);
                        auto gep = reuseGEP(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                        if (gep.created) {
                            insertInst(std::dynamic_pointer_cast<Instruction>(gep.value));
                        }
                        insertInst(StoreInst::create(val, gep.value));
                    }
                    while (idx < arraySize) {
                        auto idxConst = makeConst(context, idx++);
                        auto gep = reuseGEP(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                        if (gep.created) {
                            insertInst(std::dynamic_pointer_cast<Instruction>(gep.value));
                        }
                        insertInst(StoreInst::create(makeConst(context, 0), gep.value));
                    }
                }
            } else {
                auto alloca = AllocaInst::create(context->getIntegerTy(), name);
                insertInst(alloca, true);
                symbol = std::make_shared<IntSymbol>(name, alloca, lineno);
                if (varDef->initVal && varDef->initVal->kind == InitVal::EXP) {
                    auto val = loadIfPointer(visitExp(*varDef->initVal->exp));
                    auto store = StoreInst::create(val, alloca);
                    insertInst(store);
                }
            }
        }

        if (symbol) {
            cur_scope_->addSymbol(symbol);
        }
    }
}

void Visitor::visitDecl(const Decl &decl) {
    if (std::holds_alternative<ConstDecl>(decl)) {
        visitConstDecl(std::get<ConstDecl>(decl));
    } else if (std::holds_alternative<VarDecl>(decl)) {
        visitVarDecl(std::get<VarDecl>(decl));
    } else {
        LOG_ERROR("Unreachable in Visitor::visitDecl");
    }
}

ValuePtr Visitor::visitRelExp(const RelExp &relExp) {
    auto lhs = loadIfPointer(visitAddExp(*relExp.addExpFirst));
    if (lhs == nullptr) {
        return nullptr;
    }
    for (const auto &[op, rhs] : relExp.addExpRest) {
        auto rhsVal = loadIfPointer(visitAddExp(*rhs));
        if (rhsVal == nullptr) {
            return nullptr;
        }
        ValuePtr cmp = nullptr;
        switch (op) {
            case RelExp::LSS:
                cmp = createCmp(CompareOpType::LSS, lhs, rhsVal);
                break;
            case RelExp::GRE:
                cmp = createCmp(CompareOpType::GRE, lhs, rhsVal);
                break;
            case RelExp::LEQ:
                cmp = createCmp(CompareOpType::LEQ, lhs, rhsVal);
                break;
            case RelExp::GEQ:
                cmp = createCmp(CompareOpType::GEQ, lhs, rhsVal);
                break;
        }
        lhs = cmp;
    }
    return lhs;
}

ValuePtr Visitor::visitEqExp(const EqExp &eqExp) {
    auto lhs = loadIfPointer(visitRelExp(*eqExp.relExpFirst));
    if (lhs == nullptr) {
        return nullptr;
    }
    for (const auto &[op, rhs] : eqExp.relExpRest) {
        auto rhsVal = loadIfPointer(visitRelExp(*rhs));
        if (rhsVal == nullptr) {
            return nullptr;
        }
        ValuePtr cmp = nullptr;
        switch (op) {
            case EqExp::EQL:
                cmp = createCmp(CompareOpType::EQL, lhs, rhsVal);
                break;
            case EqExp::NEQ:
                cmp = createCmp(CompareOpType::NEQ, lhs, rhsVal);
                break;
        }
        lhs = cmp;
    }
    return lhs;
}

ValuePtr Visitor::visitLAndExp(const LAndExp &lAndExp) {
    auto ctx = ir_module_.getContext();
    auto resultAlloca = AllocaInst::create(ctx->getIntegerTy());
    insertInst(resultAlloca, true);

    auto falseBlock = newBlock("land.false");
    auto trueBlock = newBlock("land.true");
    auto endBlock = newBlock("land.end");
    auto entryBlock = newBlock("land.entry");

    if (cur_block_ != nullptr) {
        insertInst(JumpInst::create(entryBlock));
    }

    // store false once
    cur_block_ = falseBlock;
    insertInst(StoreInst::create(makeConst(ctx, 0), resultAlloca));
    insertInst(JumpInst::create(endBlock));

    cur_block_ = trueBlock;
    insertInst(StoreInst::create(makeConst(ctx, 1), resultAlloca));
    insertInst(JumpInst::create(endBlock));

    // start chain
    cur_block_ = entryBlock;
    for (size_t i = 0; i < lAndExp.eqExps.size(); ++i) {
        auto condVal = toBool(visitEqExp(*lAndExp.eqExps[i]));
        const bool isLast = (i + 1 == lAndExp.eqExps.size());
        auto next = isLast ? trueBlock : newBlock("land.next");
        insertInst(BranchInst::create(condVal, next, falseBlock));
        cur_block_ = next;
    }

    cur_block_ = endBlock;
    auto loaded = LoadInst::create(ctx->getIntegerTy(), resultAlloca);
    insertInst(loaded);
    return loaded;
}

ValuePtr Visitor::visitLOrExp(const LOrExp &lOrExp) {
    auto ctx = ir_module_.getContext();
    auto resultAlloca = AllocaInst::create(ctx->getIntegerTy());
    insertInst(resultAlloca, true);

    auto trueBlock = newBlock("lor.true");
    auto falseBlock = newBlock("lor.false");
    auto endBlock = newBlock("lor.end");
    auto entryBlock = newBlock("lor.entry");

    if (cur_block_ != nullptr) {
        insertInst(JumpInst::create(entryBlock));
    }

    // store true once
    cur_block_ = trueBlock;
    insertInst(StoreInst::create(makeConst(ctx, 1), resultAlloca));
    insertInst(JumpInst::create(endBlock));

    cur_block_ = falseBlock;
    insertInst(StoreInst::create(makeConst(ctx, 0), resultAlloca));
    insertInst(JumpInst::create(endBlock));

    // start chain
    cur_block_ = entryBlock;
    for (size_t i = 0; i < lOrExp.lAndExps.size(); ++i) {
        auto condVal = toBool(visitLAndExp(*lOrExp.lAndExps[i]));
        const bool isLast = (i + 1 == lOrExp.lAndExps.size());
        auto next = isLast ? falseBlock : newBlock("lor.next");
        insertInst(BranchInst::create(condVal, trueBlock, next));
        cur_block_ = next;
    }

    cur_block_ = endBlock;
    auto loaded = LoadInst::create(ctx->getIntegerTy(), resultAlloca);
    insertInst(loaded);
    return loaded;
}

ValuePtr Visitor::visitCond(const Cond &cond) {
    return visitLOrExp(*cond.lOrExp);
}

void Visitor::emitCondBranch(const Cond &cond, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB) {
    emitLOrBranch(*cond.lOrExp, trueBB, falseBB);
}

void Visitor::emitLOrBranch(const LOrExp &lOrExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB) {
    for (size_t i = 0; i < lOrExp.lAndExps.size(); ++i) {
        const bool isLast = (i + 1 == lOrExp.lAndExps.size());
        auto next = isLast ? falseBB : newBlock("lor.next");
        emitLAndBranch(*lOrExp.lAndExps[i], trueBB, next);
    }
}

void Visitor::emitLAndBranch(const LAndExp &lAndExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB) {
    for (size_t i = 0; i < lAndExp.eqExps.size(); ++i) {
        const bool isLast = (i + 1 == lAndExp.eqExps.size());
        auto next = isLast ? trueBB : newBlock("land.next");
        emitEqBranch(*lAndExp.eqExps[i], next, falseBB);
        if (!isLast) {
            cur_block_ = next;
        }
    }
    cur_block_ = falseBB;
}

void Visitor::emitEqBranch(const EqExp &eqExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB) {
    auto rawVal = visitEqExp(eqExp);
    if (!rawVal) {
        return;
    }
    auto condVal = toBool(rawVal);
    if (auto ci = asConstInt(condVal)) {
        insertInst(JumpInst::create(ci->getValue() != 0 ? trueBB : falseBB));
        return;
    }
    insertInst(BranchInst::create(condVal, trueBB, falseBB));
}

void Visitor::visitForVarDef(const VarDef &varDef) {
    auto context = ir_module_.getContext();
    const std::string &name = varDef.ident->content;
    const int lineno = varDef.lineno;

    if (cur_scope_->existInScope(name)) {
        ErrorReporter::error(lineno, ERR_REDEFINED_NAME);
        return;
    }

    std::shared_ptr<Symbol> symbol = nullptr;
    const bool isArray = (varDef.constExp != nullptr);
    const int arraySize = isArray && varDef.constExp ? visitConstExp(*varDef.constExp)->getValue() : 0;

    if (cur_scope_->isGlobalScope()) {
        if (isArray) {
            auto type = context->getArrayTy(context->getIntegerTy(), arraySize);
            std::vector<ConstantIntPtr> initList;
            if (varDef.initVal && varDef.initVal->kind == InitVal::LIST) {
                for (const auto &exp : varDef.initVal->list) {
                    auto val = visitExp(*exp);
                    auto ci = std::dynamic_pointer_cast<ConstantInt>(val);
                    if (!ci) {
                        if (auto evaluated = evalConstExpValue(*exp)) {
                            ci = makeConst(context, *evaluated);
                        }
                    }
                    if (ci) {
                        initList.push_back(ci);
                    }
                }
            }
            if (static_cast<int>(initList.size()) > arraySize) {
                initList.resize(arraySize);
            }
            while (static_cast<int>(initList.size()) < arraySize) {
                initList.push_back(makeConst(context, 0));
            }
            auto initVal = ConstantArray::create(type, initList);
            auto gv = GlobalVariable::create(type, name, initVal, false);
            symbol = std::make_shared<IntArraySymbol>(name, gv, lineno);
            ir_module_.addGlobalVar(gv);
        } else {
            ValuePtr initVal = nullptr;
            if (varDef.initVal && varDef.initVal->kind == InitVal::EXP) {
                auto val = visitExp(*varDef.initVal->exp);
                auto ci = std::dynamic_pointer_cast<ConstantInt>(val);
                if (!ci) {
                    if (auto evaluated = evalConstExpValue(*varDef.initVal->exp)) {
                        ci = makeConst(context, *evaluated);
                    }
                }
                if (ci) {
                    initVal = ci;
                }
            }
            auto gv = GlobalVariable::create(context->getIntegerTy(), name, initVal, false);
            symbol = std::make_shared<IntSymbol>(name, gv, lineno);
            ir_module_.addGlobalVar(gv);
        }
    } else {
        if (isArray) {
            auto type = context->getArrayTy(context->getIntegerTy(), arraySize);
            auto alloca = AllocaInst::create(type, name);
            insertInst(alloca, true);
            symbol = std::make_shared<IntArraySymbol>(name, alloca, lineno);
            if (varDef.initVal && varDef.initVal->kind == InitVal::LIST) {
                int idx = 0;
                for (const auto &exp : varDef.initVal->list) {
                    if (idx >= arraySize && arraySize > 0) break;
                    auto val = loadIfPointer(visitExp(*exp));
                    auto idxConst = makeConst(context, idx++);
                    auto gep = reuseGEP(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                    if (gep.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(gep.value));
                    }
                    insertInst(StoreInst::create(val, gep.value));
                }
                while (idx < arraySize) {
                    auto idxConst = makeConst(context, idx++);
                    auto gep = reuseGEP(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                    if (gep.created) {
                        insertInst(std::dynamic_pointer_cast<Instruction>(gep.value));
                    }
                    insertInst(StoreInst::create(makeConst(context, 0), gep.value));
                }
            }
        } else {
            auto alloca = AllocaInst::create(context->getIntegerTy(), name);
            insertInst(alloca, true);
            symbol = std::make_shared<IntSymbol>(name, alloca, lineno);
            if (varDef.initVal && varDef.initVal->kind == InitVal::EXP) {
                auto val = loadIfPointer(visitExp(*varDef.initVal->exp));
                auto store = StoreInst::create(val, alloca);
                insertInst(store);
            }
        }
    }

    if (symbol) {
        cur_scope_->addSymbol(symbol);
    }
}

void Visitor::visitForStmt(const ForStmt &forStmt) {
    if (forStmt.kind == ForStmt::DECL) {
        if (forStmt.varDef) {
            visitForVarDef(*forStmt.varDef);
        }
        return;
    }

    if (!forStmt.assign.lVal || !forStmt.assign.exp) {
        return;
    }

    if (!cur_scope_->existInSymTable(forStmt.assign.lVal->ident->content)) {
        ErrorReporter::error(forStmt.assign.lVal->lineno, ERR_UNDEFINED_NAME);
        return;
    }
    if (cur_scope_->getSymbol(forStmt.assign.lVal->ident->content)->type == CONST_INT ||
        cur_scope_->getSymbol(forStmt.assign.lVal->ident->content)->type == CONST_INT_ARRAY) {
        ErrorReporter::error(forStmt.lineno, ERR_CONST_ASSIGNMENT);
        return;
    }
    auto addr = getLValAddress(*forStmt.assign.lVal);
    auto val = loadIfPointer(visitExp(*forStmt.assign.exp));
    insertInst(StoreInst::create(val, addr));
}

bool Visitor::visitStmt(const Stmt &stmt) {
    bool hasReturn = false;

    switch (stmt.kind) {
        case Stmt::ASSIGN:
            if (!cur_scope_->existInSymTable(stmt.assignStmt.lVal->ident->content)) {
                ErrorReporter::error(stmt.lineno, ERR_UNDEFINED_NAME);
                break;
            }
            if (cur_scope_->getSymbol(stmt.assignStmt.lVal->ident->content)->type == CONST_INT ||
                cur_scope_->getSymbol(stmt.assignStmt.lVal->ident->content)->type == CONST_INT_ARRAY) {
                ErrorReporter::error(stmt.lineno, ERR_CONST_ASSIGNMENT);
            }
            if (cur_block_ != nullptr) {
                auto addr = getLValAddress(*stmt.assignStmt.lVal);
                auto val = loadIfPointer(visitExp(*stmt.assignStmt.exp));
                insertInst(StoreInst::create(val, addr));
            }
            break;
        case Stmt::EXP:
            if (stmt.exp != nullptr) {
                visitExp(*stmt.exp);
            }
            break;
        case Stmt::BLOCK:
            cur_scope_ = cur_scope_->pushScope();
            visitBlock(*stmt.block, false);
            cur_scope_ = cur_scope_->popScope();
            break;
        case Stmt::IF:
            if (stmt.ifStmt.cond != nullptr) {
                auto thenBB = newBlock("if.then");
                auto endBB = newBlock("if.end");
                BasicBlockPtr elseBB = stmt.ifStmt.elseStmt ? newBlock("if.else") : endBB;
                emitCondBranch(*stmt.ifStmt.cond, thenBB, elseBB);

                cur_block_ = thenBB;
                bool thenReturn = visitStmt(*stmt.ifStmt.thenStmt);
                if (!thenReturn && cur_block_ != nullptr) {
                    insertInst(JumpInst::create(endBB));
                }

                if (stmt.ifStmt.elseStmt != nullptr) {
                    cur_block_ = elseBB;
                    bool elseReturn = visitStmt(*stmt.ifStmt.elseStmt);
                    if (!elseReturn && cur_block_ != nullptr) {
                        insertInst(JumpInst::create(endBB));
                    }
                }

                cur_block_ = endBB;
            }
            break;
        case Stmt::FOR:
            if (stmt.forStmt.forStmtFirst &&
                stmt.forStmt.forStmtFirst->kind == ForStmt::DECL) {
                cur_scope_ = cur_scope_->pushScope();
            }
            if (stmt.forStmt.forStmtFirst != nullptr) {
                visitForStmt(*stmt.forStmt.forStmtFirst);
            }
            {
                auto condBB = newBlock("for.cond");
                auto bodyBB = newBlock("for.body");
                auto stepBB = newBlock("for.step");
                auto endBB = newBlock("for.end");

                insertInst(JumpInst::create(condBB));
                cur_block_ = condBB;

                if (stmt.forStmt.cond) {
                    emitCondBranch(*stmt.forStmt.cond, bodyBB, endBB);
                } else {
                    insertInst(JumpInst::create(bodyBB));
                }

                breakTargets_.push_back(endBB);
                continueTargets_.push_back(stepBB);

                cur_block_ = bodyBB;
                visitStmt(*stmt.forStmt.stmt);
                if (cur_block_ != nullptr) {
                    insertInst(JumpInst::create(stepBB));
                }

                cur_block_ = stepBB;
                if (stmt.forStmt.forStmtSecond != nullptr) {
                    visitForStmt(*stmt.forStmt.forStmtSecond);
                }
                insertInst(JumpInst::create(condBB));

                breakTargets_.pop_back();
                continueTargets_.pop_back();

                cur_block_ = endBB;
            }
            if (stmt.forStmt.forStmtFirst &&
                stmt.forStmt.forStmtFirst->kind == ForStmt::DECL) {
                cur_scope_ = cur_scope_->popScope();
            }
            break;
        case Stmt::BREAK:
            if (breakTargets_.empty()) {
                ErrorReporter::error(stmt.lineno, ERR_BREAK_CONTINUE_OUTSIDE_LOOP);
            } else {
                insertInst(JumpInst::create(breakTargets_.back()));
                cur_block_ = nullptr;
            }
            break;
        case Stmt::CONTINUE:
            if (continueTargets_.empty()) {
                ErrorReporter::error(stmt.lineno, ERR_BREAK_CONTINUE_OUTSIDE_LOOP);
            } else {
                insertInst(JumpInst::create(continueTargets_.back()));
                cur_block_ = nullptr;
            }
            break;
        case Stmt::RETURN:
            hasReturn = true;
            if (stmt.returnExp != nullptr) {
                auto returnExpValue = loadIfPointer(visitExp(*stmt.returnExp));
                if (cur_func_->getReturnType()->is(Type::VoidTyID) && !returnExpValue->getType()->is(Type::VoidTyID)) {
                    ErrorReporter::error(stmt.lineno, ERR_VOID_FUNC_RETURN_MISMATCH);
                }
                insertInst(ReturnInst::create(returnExpValue));
            } else {
                if (!cur_func_->getReturnType()->is(Type::VoidTyID)) {
                    // ErrorReporter::error(stmt.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
                }
                insertInst(ReturnInst::create());
            }
            cur_block_ = nullptr;
            break;
        case Stmt::PRINTF:
            auto str = stmt.printfStmt.str;
            int num = 0;

            for (size_t pos = 0; ; ) {
                pos = str.find("%d", pos);
                if (pos == std::string::npos) {
                    break;
                }
                ++num;
                pos += 2;
            }

            if (num != static_cast<int>(stmt.printfStmt.args.size())) {
                ErrorReporter::error(stmt.lineno, ERR_PRINTF_ARG_MISMATCH);
            }

            auto putchSym = cur_scope_->getFuncSymbol("putch");
            auto putintSym = cur_scope_->getFuncSymbol("putint");

            std::vector<ValuePtr> evaluatedArgs;
            evaluatedArgs.reserve(stmt.printfStmt.args.size());
            for (const auto &arg : stmt.printfStmt.args) {
                evaluatedArgs.push_back(loadIfPointer(visitExp(*arg)));
            }

            size_t argIdx = 0;
            for (size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '%' && i + 1 < str.size() && str[i + 1] == 'd') {
                    auto val = evaluatedArgs[argIdx++];
                    auto call = CallInst::create(std::dynamic_pointer_cast<Function>(putintSym->value), {val});
                    insertInst(call);
                    ++i;
                } else if (str[i] == '\\' && i + 1 < str.size() && str[i + 1] == 'n') {
                    auto call = CallInst::create(std::dynamic_pointer_cast<Function>(putchSym->value),
                        {makeConst(ir_module_.getContext(), '\n')});
                    insertInst(call);
                    ++i;
                } else if (str[i] != '\"') {
                    auto call = CallInst::create(std::dynamic_pointer_cast<Function>(putchSym->value),
                        {makeConst(ir_module_.getContext(), str[i])});
                    insertInst(call);
                }
            }
            break;
    }
    return hasReturn;
}

bool Visitor::visitBlockItem(const BlockItem &blockItem, bool isLast) {
    bool hasReturn = false;
    switch (blockItem.kind) {
        case BlockItem::DECL:
            visitDecl(*blockItem.decl);
            break;
        case BlockItem::STMT:
            hasReturn = visitStmt(*blockItem.stmt);
            break;
        default:
            LOG_ERROR("Unreachable in Visitor::visitBlockItem");
    }
    return hasReturn;
}

void Visitor::visitBlock(const Block &block, bool isFuncBlock) {
    if (block.blockItems.empty()) {
        if (isFuncBlock && !cur_func_->getReturnType()->is(Type::VoidTyID)) {
            ErrorReporter::error(block.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
        }
        return;
    }

    for (const auto &blockItem: block.blockItems) {
        if (cur_block_ == nullptr) {
            break;
        }
        if (isFuncBlock && (&blockItem == &block.blockItems.back())) {
            bool hasReturn = visitBlockItem(*blockItem, true);
            if (!hasReturn && !cur_func_->getReturnType()->is(Type::VoidTyID)) {
                ErrorReporter::error(block.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
            }
        } else {
            visitBlockItem(*blockItem, false);
        }
    }
}

FunctionPtr Visitor::visitFuncDef(const FuncDef &funcDef) {
    auto context = ir_module_.getContext();
    cseTables_.clear();

    std::vector<ArgumentPtr> paramArgs;
    std::vector<TypePtr> paramTypes;
    if (funcDef.funcFParams != nullptr) {
        for (const auto &param: funcDef.funcFParams->params) {
            // Btype is always 'int'
            if (!param->isArray) {
                auto arg = Argument::create(context->getIntegerTy(), param->ident->content);
                paramArgs.push_back(arg);
                paramTypes.push_back(context->getIntegerTy());
            } else {
                auto elementTy = context->getIntegerTy();
                auto arg = Argument::create(context->getArrayTy(elementTy), param->ident->content);
                paramArgs.push_back(arg);
                paramTypes.push_back(context->getArrayTy(elementTy));
            }
        }
    }

    auto funcValue = Function::create(
        (funcDef.funcType->kind == FuncType::VOID) ? context->getVoidTy() : context->getIntegerTy(),
        funcDef.ident->content,
        paramArgs
    );

    std::shared_ptr<Symbol> symbol;
    if (funcDef.funcType->kind == FuncType::VOID) {
        symbol = std::make_shared<VoidFuncSymbol>(funcDef.ident->content, funcValue, paramTypes, funcDef.lineno);
    } else if (funcDef.funcType->kind == FuncType::INT) {
        symbol = std::make_shared<IntFuncSymbol>(funcDef.ident->content, funcValue, paramTypes, funcDef.lineno);
    } else {
        LOG_ERROR("Unreachable in Visitor::visitFuncDef");
    }

    cur_scope_->addSymbol(symbol);
    cur_func_ = funcValue;
    cur_scope_ = cur_scope_->pushScope();
    blockId_ = 0;
    staticLocalId_ = 0;

    entry_block_ = BasicBlock::create(cur_func_);
    entry_block_->setName(funcDef.ident->content + ".entry");
    cur_block_ = entry_block_;

    if (funcDef.funcFParams != nullptr) {
        for (size_t idx = 0; idx < funcDef.funcFParams->params.size(); ++idx) {
            const auto &param = funcDef.funcFParams->params[idx];
            // Btype is always 'int'
            std::shared_ptr<Symbol> sym;
            if (!param->isArray) {
                auto allocaValue = AllocaInst::create(context->getIntegerTy());
                insertInst(allocaValue, true);
                auto store = StoreInst::create(paramArgs[idx], allocaValue);
                insertInst(store);
                sym = std::make_shared<IntSymbol>(
                    param->ident->content, allocaValue, param->lineno);
            } else {
                sym = std::make_shared<IntArraySymbol>(
                    param->ident->content, paramArgs[idx], param->lineno);
            }
            cur_scope_->addSymbol(sym);
        }
    }

    visitBlock(*funcDef.block, true);

    cur_scope_ = cur_scope_->popScope();

    if (cur_block_ != nullptr && funcDef.funcType->kind == FuncType::VOID) {
        insertInst(ReturnInst::create());
    }
    runDCE(cur_func_);
    ir_module_.addFunction(funcValue);
    cur_block_ = nullptr;
    entry_block_ = nullptr;
    staticLocalId_ = 0;
    return funcValue;
}

FunctionPtr Visitor::visitMainFuncDef(const MainFuncDef &mainFunc) {
    cur_func_ = Function::create(
        ir_module_.getContext()->getIntegerTy(),
        "main",
        {}
    );
    blockId_ = 0;
    staticLocalId_ = 0;
    cur_scope_ = cur_scope_->pushScope();
    entry_block_ = BasicBlock::create(cur_func_);
    entry_block_->setName("main.entry");
    cur_block_ = entry_block_;
    visitBlock(*mainFunc.block, true);
    cur_scope_ = cur_scope_->popScope();
    if (cur_block_ != nullptr) {
        insertInst(ReturnInst::create(makeConst(ir_module_.getContext(), 0)));
    }
    runDCE(cur_func_);
    ir_module_.addFunction(cur_func_);
    entry_block_ = nullptr;
    cur_block_ = nullptr;
    return cur_func_;
}

void Visitor::visit(const CompUnit &compUnit) {
    auto context = ir_module_.getContext();

    auto addBuiltin = [&](const std::string &name, TypePtr ret, const std::vector<TypePtr> &params) {
        std::vector<ArgumentPtr> args;
        for (size_t i = 0; i < params.size(); ++i) {
            args.push_back(Argument::create(params[i], name + ".arg" + std::to_string(i)));
        }
        auto func = Function::create(ret, name, args);
        cur_scope_->addSymbol(std::make_shared<FuncSymbol>(
            ret->is(Type::VoidTyID) ? VOID_FUNC : INT_FUNC, name, func, params, -1));
        return func;
    };

    addBuiltin("getint", context->getIntegerTy(), {});
    addBuiltin("putint", context->getVoidTy(), {context->getIntegerTy()});
    addBuiltin("putch", context->getVoidTy(), {context->getIntegerTy()});
    addBuiltin("putstr", context->getVoidTy(), {context->getArrayTy(context->getIntegerTy())});

    for (const auto &var_decl: compUnit.decls) {
        visitDecl(*var_decl);
    }

    for (const auto &func_def: compUnit.func_defs) {
        visitFuncDef(*func_def);
    }

    const auto mainFuncPtr = visitMainFuncDef(*compUnit.main_func);
    ir_module_.setMainFunction(mainFuncPtr);
}
