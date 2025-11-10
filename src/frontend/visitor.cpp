#include "visitor.h"

#include "logger.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/Argument.h"
#include "llvm/include/ir/value/ConstantArray.h"
#include "llvm/include/ir/value/ConstantInt.h"
#include "llvm/include/ir/value/GlobalVariable.h"
#include "llvm/include/ir/value/inst/BinaryInstruction.h"
#include "llvm/include/ir/value/inst/UnaryInstruction.h"

std::shared_ptr<Value> Visitor::visitPrimaryExp(const PrimaryExp &primaryExp) {
    switch (primaryExp.kind) {
        case PrimaryExp::EXP:
            return visitExp(*primaryExp.exp);

        case PrimaryExp::LVAL: {
            const std::string &name = primaryExp.lval->ident->content;
            if (!cur_scope_->existInSymTable(name)) {
                ErrorReporter::error(primaryExp.lineno, ERR_UNDEFINED_NAME);
                return nullptr;
            }
            auto symbol = cur_scope_->getSymbol(name);
            return symbol->value;  // symbol里保存的Value，比如AllocaInst或Constant
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

std::shared_ptr<Value> Visitor::visitUnaryExp(const UnaryExp &unaryExp) {
    switch (unaryExp.kind) {
        case UnaryExp::PRIMARY:
            return visitPrimaryExp(*unaryExp.primary);

        case UnaryExp::CALL: {
            if (!cur_scope_->existInSymTable(unaryExp.call->ident->content)) {
                ErrorReporter::error(unaryExp.lineno, ERR_UNDEFINED_NAME);
            }

            auto symbol = cur_scope_->getFuncSymbol(unaryExp.call->ident->content);
            if (symbol == nullptr) {
                ErrorReporter::error(unaryExp.lineno, ERR_UNDEFINED_NAME);
                return nullptr;
            }
            std::vector<std::shared_ptr<Value>> args;

            if (unaryExp.call->params) {
                // check params count
                if (unaryExp.call->params->params.size() != symbol->getParamCnt()) {
                    ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_COUNT_MISMATCH);
                }

                // check params type
                for (size_t i = 0; i < unaryExp.call->params->params.size(); ++i) {
                    auto expValue = visitExp(*unaryExp.call->params->params[i]);
                    auto paramType = symbol->params[i];
                    // TODO: const array
                    if (paramType != expValue->getType()) {
                        ErrorReporter::error(unaryExp.lineno, ERR_FUNC_ARG_TYPE_MISMATCH);
                    }
                    args.push_back(expValue);
                }
            }
            return CallInst::create(std::dynamic_pointer_cast<Function>(symbol->value), args);

        }

        case UnaryExp::UNARY_OP: {
            auto rhs = visitUnaryExp(*unaryExp.unary->expr);
            switch (unaryExp.unary->op->kind) {
                case UnaryOp::PLUS:
                    return UnaryOperator::create(UnaryOpType::Pos, rhs);
                case UnaryOp::MINU:
                    return UnaryOperator::create(UnaryOpType::Neg, rhs);
                case UnaryOp::NOT:
                    return UnaryOperator::create(UnaryOpType::Not, rhs);
                default:
                    LOG_ERROR("Unreachable in Visitor::visitUnaryExp");
                    return nullptr;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<Value> Visitor::visitMulExp(const MulExp &mulExp) {
    auto lhs = visitUnaryExp(*mulExp.first);
    for (const auto &[op, rhs] : mulExp.rest) {
        auto rhsVal = visitUnaryExp(*rhs);
        switch (op) {
            case MulExp::MULT:
                lhs = BinaryOperator::create(BinaryOpType::Mul, lhs, rhsVal);
                break;
            case MulExp::DIV:
                lhs = BinaryOperator::create(BinaryOpType::Div, lhs, rhsVal);
                break;
            case MulExp::MOD:
                lhs = BinaryOperator::create(BinaryOpType::Mod, lhs, rhsVal);
                break;
        }
    }
    return lhs;
}

std::shared_ptr<Value> Visitor::visitAddExp(const AddExp &addExp) {
    auto lhs = visitMulExp(*addExp.first);
    for (const auto &[op, rhs] : addExp.rest) {
        auto rhsVal = visitMulExp(*rhs);
        switch (op) {
            case AddExp::PLUS:
                lhs = BinaryOperator::create(BinaryOpType::Add, lhs, rhsVal);
                break;
            case AddExp::MINU:
                lhs = BinaryOperator::create(BinaryOpType::Sub, lhs, rhsVal);
                break;
        }
    }
    return lhs;
}

std::shared_ptr<ConstantInt> Visitor::visitConstExp(const ConstExp &constExp) {
    auto value = visitAddExp(*constExp.addExp);

    if (value->getValueType() != ValueType::ConstantIntTy) {
        LOG_ERROR("ConstExp did not evaluate to a ConstantInt");
        return nullptr;
    }

    return std::dynamic_pointer_cast<ConstantInt>(value);
}

std::shared_ptr<Value> Visitor::visitExp(const Exp &exp) {
    return visitAddExp(*exp.addExp);
}

void Visitor::visitConstDecl(const ConstDecl &constDecl) {
    auto context = ir_module_.getContext();

    for (const auto &constDef: constDecl.constDefs) {
        const std::string &name = constDef->ident->content;
        const int lineno = constDef->lineno;
        std::shared_ptr<Symbol> symbol;
        std::shared_ptr<Constant> value;

        if (constDef->constExp == nullptr) {
            // --- 普通常量 ---
            value = ConstantInt::create(context->getIntegerTy(), 0);

            symbol = std::make_shared<ConstIntSymbol>(name, value, lineno);
        } else {
            // --- 常量数组 ---
            auto arrayType = context->getArrayTy(context->getIntegerTy());

            auto initialValList = std::vector<std::shared_ptr<ConstantInt>>{};
            if (constDef->constInitVal->kind == ConstInitVal::LIST) {
                for (const auto &constExp : constDef->constInitVal->list) {
                    auto val = visitConstExp(*constExp);
                    initialValList.push_back(val);
                }
            } else {
                LOG_ERROR("Unreachable in Visitor::visitConstDecl");
            }

            value = ConstantArray::create(arrayType, initialValList);

            symbol = std::make_shared<ConstIntArraySymbol>(name, value, lineno);
        }

        cur_scope_->addSymbol(symbol);
    }
}

void Visitor::visitVarDecl(const VarDecl &varDecl) {
    const bool isStatic = (varDecl.prefix == "static");

    auto context = ir_module_.getContext();

    for (const auto &varDef: varDecl.varDefs) {
        const std::string &name = varDef->ident->content;
        const int lineno = varDef->lineno;
        std::shared_ptr<Value> value = nullptr;
        std::shared_ptr<Symbol> symbol = nullptr;
        const bool isArray = (varDef->constExp != nullptr);

        if (isStatic || cur_scope_->isGlobalScope()) {
            // --- static 或 global ---
            if (isArray) {
                auto type = context->getArrayTy(context->getIntegerTy());
                auto gv = GlobalVariable::create(type, name, false);
                if (isStatic) {
                    symbol = std::make_shared<StaticIntArraySymbol>(name, gv, lineno);
                } else {
                    symbol = std::make_shared<IntArraySymbol>(name, gv, lineno);
                }
            } else {
                auto gv = GlobalVariable::create(context->getIntegerTy(), name, false);
                if (isStatic) {
                    symbol = std::make_shared<StaticIntSymbol>(name, gv, lineno);
                } else {
                    symbol = std::make_shared<IntSymbol>(name, gv, lineno);
                }
            }
        } else {
            // --- 普通局部变量 ---
            if (isArray) {
                auto type = context->getArrayTy(context->getIntegerTy());
                auto alloca = AllocaInst::create(type);
                symbol = std::make_shared<IntArraySymbol>(name, alloca, lineno);
            } else {
                auto alloca = AllocaInst::create(context->getIntegerTy());
                symbol = std::make_shared<IntSymbol>(name, alloca, lineno);
            }
        }

        cur_scope_->addSymbol(symbol);
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

void Visitor::visitForStmt(const ForStmt &forStmt) {
    for (const auto &[lVal, exp] : forStmt.assigns) {
        if (cur_scope_->getSymbol(lVal->ident->content)->type == CONST_INT ||
                cur_scope_->getSymbol(lVal->ident->content)->type == CONST_INT_ARRAY) {
            ErrorReporter::error(forStmt.lineno, ERR_CONST_ASSIGNMENT);
        }
    }
}

void Visitor::visitStmt(const Stmt &stmt, bool isLast) {
    if (isLast && !cur_func_->getReturnType()->is(Type::VoidTyID) && stmt.kind != Stmt::RETURN) {
        ErrorReporter::error(stmt.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
    }

    switch (stmt.kind) {
        case Stmt::ASSIGN:
            if (!cur_scope_->existInSymTable(stmt.assignStmt.lVal->ident->content)) {
                ErrorReporter::error(stmt.lineno, ERR_UNDEFINED_NAME);
            }
            if (cur_scope_->getSymbol(stmt.assignStmt.lVal->ident->content)->type == CONST_INT ||
                cur_scope_->getSymbol(stmt.assignStmt.lVal->ident->content)->type == CONST_INT_ARRAY) {
                ErrorReporter::error(stmt.lineno, ERR_CONST_ASSIGNMENT);
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
            if (stmt.ifStmt.thenStmt != nullptr) {
                visitStmt(*stmt.ifStmt.thenStmt, false);
            }
            if (stmt.ifStmt.elseStmt != nullptr) {
                visitStmt(*stmt.ifStmt.elseStmt, false);
            }
            break;
        case Stmt::FOR:
            inForLoop_ = true;
            if (stmt.forStmt.forStmtFirst != nullptr) {
                visitForStmt(*stmt.forStmt.forStmtFirst);
            }
            if (stmt.forStmt.forStmtSecond != nullptr) {
                visitForStmt(*stmt.forStmt.forStmtSecond);
            }
            visitStmt(*stmt.forStmt.stmt, false);
            inForLoop_ = false;
            break;
        case Stmt::BREAK:
            if (!inForLoop_) {
                ErrorReporter::error(stmt.lineno, ERR_BREAK_CONTINUE_OUTSIDE_LOOP);
            }
            break;
        case Stmt::CONTINUE:
            if (!inForLoop_) {
                ErrorReporter::error(stmt.lineno, ERR_BREAK_CONTINUE_OUTSIDE_LOOP);
            }
            break;
        case Stmt::RETURN:
            if (stmt.returnExp != nullptr) {
                auto returnExpValue = visitExp(*stmt.returnExp);
                if (cur_func_->getReturnType()->is(Type::VoidTyID) && !returnExpValue->getType()->is(Type::VoidTyID)) {
                    ErrorReporter::error(stmt.lineno, ERR_VOID_FUNC_RETURN_MISMATCH);
                }
            } else {
                if (!cur_func_->getReturnType()->is(Type::VoidTyID)) {
                    ErrorReporter::error(stmt.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
                }
            }
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
            break;
    }
}

void Visitor::visitBlockItem(const BlockItem &blockItem, bool isLast) {
    switch (blockItem.kind) {
        case BlockItem::DECL:
            if (isLast && !cur_func_->getReturnType()->is(Type::VoidTyID)) {
                ErrorReporter::error(blockItem.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
            }
            visitDecl(*blockItem.decl);
            break;
        case BlockItem::STMT:
            visitStmt(*blockItem.stmt, isLast);
            break;
        default:
            LOG_ERROR("Unreachable in Visitor::visitBlockItem");
    }
}

void Visitor::visitBlock(const Block &block, bool isFuncBlock) {
    if (block.blockItems.empty()) {
        if (isFuncBlock && !cur_func_->getReturnType()->is(Type::VoidTyID)) {
            ErrorReporter::error(block.lineno, ERR_NONVOID_FUNC_MISSING_RETURN);
        }
        return;
    }
    for (const auto &blockItem: block.blockItems) {
        visitBlockItem(*blockItem, isFuncBlock && (&blockItem == &block.blockItems.back()));
    }
}

void Visitor::visitFuncDef(const FuncDef &funcDef) {
    // check functionDef redefinition in current scope
    if (cur_scope_->existInSymTable(funcDef.ident->content)) {
        ErrorReporter::error(funcDef.lineno, ERR_REDEFINED_NAME);
    }

    auto context = ir_module_.getContext();

    std::vector<ArgumentPtr> paramArgs;
    std::vector<TypePtr> paramTypes;
    if (funcDef.funcFParams != nullptr) {
        for (const auto &param: funcDef.funcFParams->params) {
            // Btype is always 'int'
            if (!param->isArray) {
                auto arg = Argument::Create(context->getIntegerTy(), param->ident->content);
                paramArgs.push_back(arg);
                paramTypes.push_back(context->getIntegerTy());
            } else {
                auto elementTy = context->getIntegerTy();
                auto arg = Argument::Create(context->getArrayTy(elementTy), param->ident->content);
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

    if (funcDef.funcFParams != nullptr) {
        for (const auto &param: funcDef.funcFParams->params) {
            // Btype is always 'int'
            std::shared_ptr<Symbol> sym;
            if (!param->isArray) {
                auto allocaValue = AllocaInst::create(context->getIntegerTy());
                sym = std::make_shared<IntSymbol>(
                    param->ident->content, allocaValue, param->lineno);
            } else {
                auto elementTy = context->getIntegerTy();
                auto allocaValue = AllocaInst::create(context->getArrayTy(elementTy));
                sym = std::make_shared<IntArraySymbol>(
                    param->ident->content, allocaValue, param->lineno);
            }
            cur_scope_->addSymbol(sym);
        }
    }

    visitBlock(*funcDef.block, true);

    cur_scope_ = cur_scope_->popScope();
}

void Visitor::visitMainFuncDef(const MainFuncDef &mainFunc) {
    cur_func_ = Function::create(
        ir_module_.getContext()->getIntegerTy(),
        "main",
        {}
    );
    cur_scope_ = cur_scope_->pushScope();
    visitBlock(*mainFunc.block, true);
    cur_scope_ = cur_scope_->popScope();
}

void Visitor::visit(const CompUnit &compUnit) {
    for (const auto &var_decl: compUnit.decls) {
        visitDecl(*var_decl);
    }

    for (const auto &func_def: compUnit.func_defs) {
        visitFuncDef(*func_def);
    }

    visitMainFuncDef(*compUnit.main_func);

    cur_scope_->printAllScopes();
}
