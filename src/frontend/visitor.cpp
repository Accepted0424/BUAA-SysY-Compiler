#include "visitor.h"

#include <functional>
#include <optional>

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
        return;
    }
    if (cur_block_ != nullptr) {
        cur_block_->insertInstruction(inst);
    }
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
            auto load = LoadInst::create(ir_module_.getContext()->getIntegerTy(), value);
            insertInst(load);
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
        auto zext = ZExtInst::create(ctxInt32, value);
        insertInst(zext);
        return zext;
    }
    return value;
}

ValuePtr Visitor::createCmp(CompareOpType op, ValuePtr lhs, ValuePtr rhs) {
    lhs = loadIfPointer(lhs);
    rhs = loadIfPointer(rhs);
    lhs = zextToInt32(lhs);
    rhs = zextToInt32(rhs);
    auto cmp = CompareOperator::create(op, lhs, rhs);
    insertInst(cmp);
    return cmp;
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
        auto gep = GetElementPtrInst::create(gepType, base, indices);
        insertInst(gep);
        base = gep;
    }
    return base;
}

ValuePtr Visitor::visitPrimaryExp(const PrimaryExp &primaryExp) {
    switch (primaryExp.kind) {
        case PrimaryExp::EXP:
            return visitExp(*primaryExp.exp);

        case PrimaryExp::LVAL: {
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
                                auto decay = GetElementPtrInst::create(paramType, argVal, {zero, zero});
                                insertInst(decay);
                                argVal = decay;
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
                    res = UnaryOperator::create(UnaryOpType::POS, rhs);
                    break;
                case UnaryOp::MINU:
                    res = UnaryOperator::create(UnaryOpType::NEG, rhs);
                    break;
                case UnaryOp::NOT:
                    res = createCmp(CompareOpType::EQL, rhs, makeConst(ir_module_.getContext(), 0));
                    break;
                default:
                    LOG_ERROR("Unreachable in Visitor::visitUnaryExp");
                    return nullptr;
            }
            if (res && res->getValueType() != ValueType::CompareInstTy) {
                insertInst(std::dynamic_pointer_cast<Instruction>(res));
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
        switch (op) {
            case MulExp::MULT:
                lhs = BinaryOperator::create(BinaryOpType::MUL, lhs, rhsVal);
                insertInst(std::dynamic_pointer_cast<Instruction>(lhs));
                break;
            case MulExp::DIV:
                lhs = BinaryOperator::create(BinaryOpType::DIV, lhs, rhsVal);
                insertInst(std::dynamic_pointer_cast<Instruction>(lhs));
                break;
            case MulExp::MOD:
                lhs = BinaryOperator::create(BinaryOpType::MOD, lhs, rhsVal);
                insertInst(std::dynamic_pointer_cast<Instruction>(lhs));
                break;
        }
    }
    return lhs;
}

ValuePtr Visitor::visitAddExp(const AddExp &addExp) {
    auto lhs = loadIfPointer(visitMulExp(*addExp.first));
    for (const auto &[op, rhs] : addExp.rest) {
        auto rhsVal = loadIfPointer(visitMulExp(*rhs));
        switch (op) {
            case AddExp::PLUS:
                lhs = BinaryOperator::create(BinaryOpType::ADD, lhs, rhsVal);
                insertInst(std::dynamic_pointer_cast<Instruction>(lhs));
                break;
            case AddExp::MINU:
                lhs = BinaryOperator::create(BinaryOpType::SUB, lhs, rhsVal);
                insertInst(std::dynamic_pointer_cast<Instruction>(lhs));
                break;
        }
    }
    return lhs;
}

ConstantIntPtr Visitor::visitConstExp(const ConstExp &constExp) {
    if (auto valOpt = evalConstConstExp(constExp)) {
        return ConstantInt::create(ir_module_.getContext()->getIntegerTy(), *valOpt);
    }
    auto ctx = ir_module_.getContext();

    std::function<int(const AddExp&)> evalAdd;
    std::function<int(const MulExp&)> evalMul;
    std::function<int(const UnaryExp&)> evalUnary;
    std::function<int(const PrimaryExp&)> evalPrimary;

    evalPrimary = [&](const PrimaryExp &p) -> int {
        switch (p.kind) {
            case PrimaryExp::NUMBER:
                return std::stoi(p.number->value);
            case PrimaryExp::EXP:
                return evalAdd(*p.exp->addExp);
            case PrimaryExp::LVAL:
            default:
                return 0;
        }
    };

    evalUnary = [&](const UnaryExp &u) -> int {
        if (u.kind == UnaryExp::PRIMARY) return evalPrimary(*u.primary);
        if (u.kind == UnaryExp::UNARY_OP) {
            auto val = evalUnary(*u.unary->expr);
            switch (u.unary->op->kind) {
                case UnaryOp::PLUS: return val;
                case UnaryOp::MINU: return -val;
                case UnaryOp::NOT: return !val;
            }
        }
        return 0;
    };

    evalMul = [&](const MulExp &m) -> int {
        int res = evalUnary(*m.first);
        for (const auto &[op, rhs] : m.rest) {
            int rhsVal = evalUnary(*rhs);
            switch (op) {
                case MulExp::MULT: res *= rhsVal; break;
                case MulExp::DIV: res /= rhsVal; break;
                case MulExp::MOD: res %= rhsVal; break;
            }
        }
        return res;
    };

    evalAdd = [&](const AddExp &a) -> int {
        int res = evalMul(*a.first);
        for (const auto &[op, rhs] : a.rest) {
            int rhsVal = evalMul(*rhs);
            if (op == AddExp::PLUS) res += rhsVal;
            else res -= rhsVal;
        }
        return res;
    };

    int val = evalAdd(*constExp.addExp);
    return ConstantInt::create(ctx->getIntegerTy(), val);
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
                    // local
                    auto alloca = AllocaInst::create(context->getIntegerTy(), name);
                    insertInst(alloca, true);
                    auto store = StoreInst::create(val, alloca);
                    insertInst(store);
                    symbol->value = alloca;
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
                    auto gep = GetElementPtrInst::create(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                    insertInst(gep);
                    auto store = StoreInst::create(initialValList[i], gep);
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
                        auto gep = GetElementPtrInst::create(context->getIntegerTy(), alloca, {makeConst(context, 0), idxConst});
                        insertInst(gep);
                        insertInst(StoreInst::create(val, gep));
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

void Visitor::visitForStmt(const ForStmt &forStmt) {
    for (const auto &[lVal, exp] : forStmt.assigns) {
        if (!cur_scope_->existInSymTable(lVal->ident->content)) {
            ErrorReporter::error(lVal->lineno, ERR_UNDEFINED_NAME);
            break;
        }
        if (cur_scope_->getSymbol(lVal->ident->content)->type == CONST_INT ||
            cur_scope_->getSymbol(lVal->ident->content)->type == CONST_INT_ARRAY) {
            ErrorReporter::error(forStmt.lineno, ERR_CONST_ASSIGNMENT);
            continue;
        }
        auto addr = getLValAddress(*lVal);
        auto val = loadIfPointer(visitExp(*exp));
        insertInst(StoreInst::create(val, addr));
    }
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
                auto condVal = toBool(visitCond(*stmt.ifStmt.cond));
                auto thenBB = newBlock("if.then");
                auto endBB = newBlock("if.end");
                BasicBlockPtr elseBB = stmt.ifStmt.elseStmt ? newBlock("if.else") : endBB;
                insertInst(BranchInst::create(condVal, thenBB, elseBB));

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
            inForLoop_ = true;
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

                ValuePtr condVal = stmt.forStmt.cond ? toBool(visitCond(*stmt.forStmt.cond)) : makeConst(ir_module_.getContext(), 1);
                insertInst(BranchInst::create(condVal, bodyBB, endBB));

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
            inForLoop_ = false;
            break;
        case Stmt::BREAK:
            if (!inForLoop_) {
                ErrorReporter::error(stmt.lineno, ERR_BREAK_CONTINUE_OUTSIDE_LOOP);
            } else {
                insertInst(JumpInst::create(breakTargets_.back()));
                cur_block_ = nullptr;
            }
            break;
        case Stmt::CONTINUE:
            if (!inForLoop_) {
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

    ir_module_.addFunction(funcValue);

    if (cur_block_ != nullptr && funcDef.funcType->kind == FuncType::VOID) {
        insertInst(ReturnInst::create());
    }
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
