#include "visitor.h"

#include "logger.h"
#include "llvm/include/ir/module.h"
#include "llvm/include/ir/value/BasicBlock.h"
#include "llvm/include/ir/value/BasicBlock.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/GlobalVariable.h"
#include "llvm/include/ir/value/User.h"
#include "llvm/include/ir/value/inst/Instruction.h"

void Visitor::visitFuncDef(const FuncDef &funcDef) {
    if (node.ident == nullptr) {
        // invalid ast
        return;
    }
}

ValuePtr Visitor::visitPrimaryExp(const PrimaryExp &primaryExp) {
    switch (primaryExp.kind) {
        case PrimaryExp::EXP:

        case PrimaryExp::LVAL:

        case PrimaryExp::NUMBER:

        default:



    }
}

ValuePtr Visitor::visitUnaryExp(const UnaryExp &unaryExp) {
    switch (unaryExp.kind) {
        case UnaryExp::PRIMARY:

        case UnaryExp::CALL:

        case UnaryExp::UNARY_OP:

        default: ;
    }
}

std::shared_ptr<Value> Visitor::visitMulExp(const MulExp &mulExp) {
}

std::shared_ptr<Value> Visitor::visitAddExp(const AddExp &addExp) {
    auto lhs = visitMulExp(*addExp.first);

    for (const auto &[op, rhs_exp]: addExp.rest) {
        auto rhs = visitMulExp(*rhs_exp);

        switch (op) {
            case AddExp::PLUS: {
                lhs =
                break;
            }
            case AddExp::MINU: {
                lhs = new SubInst(lhs, rhs);
                break;
            }
            default:
                LOG_ERROR("Unreachable in Visitor::visitAddExp");
        }
    }
}

std::shared_ptr<Value> Visitor::visitExp(const Exp &exp) {
    return visitAddExp(*exp.addExp);
}

int Visitor::evaluateConstExp(const ConstExp &constExp) {
    auto value = visitAddExp(*constExp.addExp);
}

void Visitor::visitConstDef(const ConstDef &constDef) {
    auto context = ir_module_.getContext();

    std::shared_ptr<Symbol> symbol;

    if (cur_func_ == nullptr) {
        // global constant

        if (constDef.constExp == nullptr) {
            // scalar constant
            auto globalScalarConst = GlobalVariable::create(
                context->getIntegerTy(), constDef.ident->content, true);

            symbol = std::make_shared<ConstIntSymbol>(
                constDef.ident->content, globalScalarConst, constDef.lineno);
        } else {
            // array constant
            int num = evaluateConstExp(*constDef.constExp);

            auto arrayType = context->getArrayTy(context->getIntegerTy(), num);

            auto globalConstArray = GlobalVariable::create(
                arrayType, constDef.ident->content, true);

            symbol = std::make_shared<ConstIntArraySymbol>(
                constDef.ident->content, globalConstArray, constDef.lineno);
        }
    } else {
        // local constant
        auto alloca = AllocaInst::create(context->getIntegerTy());

        symbol = std::make_shared<ConstIntSymbol>(
            constDef.ident->content, alloca, constDef.lineno);

        (*cur_func_->BasicBlockBegin())->insertInstruction(alloca);
    }

    cur_scope_->addSymbol(symbol);
}

void Visitor::visitConstDecl(const ConstDecl &constDecl) {
    // In this toy compiler, btype is always 'int', so we ignore it.
    for (const auto &constDef: constDecl.constDefs) {
        visitConstDef(*constDef);
    }
}

void Visitor::visitVarDecl(const VarDecl &varDecl) {
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

void Visitor::visitStmt(const Stmt &stmt) {
    switch (stmt.kind) {
        case Stmt::ASSIGN:
            visitLVal(*stmt.assignStmt.lVal);
            visitExp(*stmt.assignStmt.exp);

            break;
        case Stmt::EXP:
            // visitPutStmt(*stmt.putStmt);
            break;
        case Stmt::BLOCK:
            // visitTagStmt(*stmt.tagStmt);
            break;
        case Stmt::IF:
            // visitLetStmt(*stmt.letStmt);
            break;
        case Stmt::FOR:
            // visitIfStmt(*stmt.ifStmt);
            break;
        case Stmt::BREAK:
            // visitToStmt(*stmt.toStmt);
            break;
        case Stmt::CONTINUE:
            break;
        case Stmt::RETURN:
            break;
        case Stmt::PRINTF:
            break;
        default:
            LOG_ERROR("Unreachable in Visitor::visitStmt");
    }
}

void Visitor::visitBlockItem(const BlockItem &blockItem) {
    switch (blockItem.kind) {
        case BlockItem::DECL:
            visitDecl(*blockItem.decl);
            break;
        case BlockItem::STMT:
            visitStmt(*blockItem.stmt);
            break;
        default:
            LOG_ERROR("Unreachable in Visitor::visitBlockItem");
    }
}

void Visitor::visitBlock(const Block &block) {
    for (const auto &blockItem: block.blockItems) {
        visitBlockItem(*blockItem);
    }
}

void Visitor::visitMainFuncDef(const MainFuncDef &mainFunc) {
    // create ir function
    const auto context = ir_module_.getContext();
    cur_func_ = Function::create(context->getIntegerTy(), "main");
    ir_module_.addMainFunction(*cur_func_);
    cur_block_ = cur_func_->NewBasicBlock();
    cur_scope_ = cur_scope_->pushScope();
    visitBlock(*mainFunc.block);
}

void Visitor::visit(const CompUnit &compUnit) {
    for (const auto &var_decl: compUnit.decls) {
        visitDecl(*var_decl);
    }

    for (const auto &func_def: compUnit.func_defs) {
        visitFuncDef(*func_def);
    }

    visitMainFuncDef(*compUnit.main_func);

    cur_func_ = nullptr;
    cur_block_ = nullptr;
}
