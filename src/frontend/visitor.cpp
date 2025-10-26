#include "visitor.h"

#include "logger.h"
#include "llvm/include/ir/value/User.h"

void Visitor::visitFuncDef(const AstNode::FuncDef &funcDef) {
    if (node.ident == nullptr) { // invalid ast
        return;
    }


}

ValuePtr Visitor::visitPrimaryExp(const AstNode::PrimaryExp &primaryExp) {
    switch (primaryExp.kind) {
        case AstNode::PrimaryExp::EXP:

        case AstNode::PrimaryExp::LVAL:

        case AstNode::PrimaryExp::NUMBER:

        default:
    }
}

ValuePtr Visitor::visitUnaryExp(const AstNode::UnaryExp &unaryExp) {
    switch (unaryExp.kind) {
        case AstNode::UnaryExp::PRIMARY:

        case AstNode::UnaryExp::CALL:

        case AstNode::UnaryExp::UNARY_OP:

        default: ;
    }
}

ValuePtr Visitor::visitMulExp(const AstNode::MulExp &mulExp) {

}

ValuePtr Visitor::visitAddExp(const AstNode::AddExp &addExp) {

}

ValuePtr Visitor::visitExp(const AstNode::Exp &exp) {

}

void Visitor::visitConstDef(const AstNode::ConstDef &constDef) {
    if (constDef.constExp != nullptr) {
        auto constIntSym = std::make_shared<ConstIntSymbol>(
            constDef.ident->content, constDef.constInitVal->, constDef.lineno);
    }
}

void Visitor::visitConstDecl(const AstNode::ConstDecl &constDecl) {

}

void Visitor::visitVarDecl(const AstNode::VarDecl &varDecl) {

}

void Visitor::visitDecl(const AstNode::Decl &decl) {
    if (std::holds_alternative<AstNode::ConstDecl>(decl)) {
        visitConstDecl(std::get<AstNode::ConstDecl>(decl));
    } else if (std::holds_alternative<AstNode::VarDecl>(decl)) {
        visitVarDecl(std::get<AstNode::VarDecl>(decl));
    } else {
        LOG_ERROR("Unreachable in Visitor::visitDecl");
    }
}

void visitMainFuncDef(const AstNode::MainFuncDef &node) {
    // create ir function
    const auto context = ir_module_->Context();
    auto func = Function::New(context->GetFloatTy(), "main");
    ir_module_->AddMainFunction(func);
}

void Visitor::visit(const AstNode::CompUnit &compUnit) {
    for (const auto &var_decl : compUnit.decls) {
        visitDecl(*var_decl);
    }

    for (const auto &func_def : compUnit.func_defs) {
        visitFuncDef(*func_def);
    }

}