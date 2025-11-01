# pragma once

#include <memory>

#include "ast.h"
#include "symtable.h"
#include "llvm/include/ir/module.h"
#include "llvm/include/ir/value/BasicBlock.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/Value.h"

using namespace AstNode;

class Visitor {
public:
    Visitor(Module &module)
        : ir_module_(module),
          cur_scope_(std::make_shared<SymbolTable>(nullptr)) {}

    void visit(const CompUnit &node);

private:
    Module &ir_module_;

    std::shared_ptr<SymbolTable> cur_scope_;

    Function* cur_func_ = nullptr;

    BasicBlock* cur_block_ = nullptr;

    void visitFuncDef(const FuncDef &node);

    void visitMainFuncDef(const MainFuncDef &mainFunc);

    std::shared_ptr<Value> visitMulExp(const MulExp &mulExp);

    std::shared_ptr<Value> visitAddExp(const AddExp &addExp);

    std::shared_ptr<Value> visitExp(const Exp &exp);

    int evaluateConstExp(const ConstExp &constExp);

    void visitConstDef(const ConstDef &constDef);

    void visitDecl(const Decl &elm);

    void visitStmt(const Stmt &stmt);

    void visitBlockItem(const BlockItem &blockItem);

    void visitBlock(const Block &block);
};
