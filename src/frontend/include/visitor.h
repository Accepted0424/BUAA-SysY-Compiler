# pragma once

#include <memory>

#include "ast.h"
#include "symtable.h"
#include "llvm/include/ir/module.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/Value.h"

using namespace AstNode;

class Visitor {
public:
    Visitor(Module &module)
        : ir_module_(module),
          cur_scope_(std::make_shared<SymbolTable>()) {}

    Visitor(Module &module, std::ofstream& out)
        : out_(out),
          ir_module_(module),
          cur_scope_(std::make_shared<SymbolTable>(out)) {}

    void visit(const CompUnit &node);

private:
    std::optional<std::reference_wrapper<std::ofstream>> out_;

    Module &ir_module_;

    std::shared_ptr<SymbolTable> cur_scope_;

    FunctionPtr cur_func_ = nullptr;

    // BasicBlockPtr cur_block_ = nullptr;

    bool inForLoop_ = false;

    void visitFuncDef(const FuncDef &node);

    void visitMainFuncDef(const MainFuncDef &mainFunc);

    std::shared_ptr<Value> visitPrimaryExp(const PrimaryExp &primaryExp);

    std::shared_ptr<Value> visitUnaryExp(const UnaryExp &unaryExp);

    std::shared_ptr<Value> visitMulExp(const MulExp &mulExp);

    std::shared_ptr<Value> visitAddExp(const AddExp &addExp);

    std::shared_ptr<ConstantInt> visitConstExp(const ConstExp &constExp);

    std::shared_ptr<Value> visitExp(const Exp &exp);

    void visitConstDecl(const ConstDecl &constDecl);

    void visitVarDecl(const VarDecl &varDecl);

    void visitDecl(const Decl &decl);

    void visitForStmt(const ForStmt &forStmt);

    void visitStmt(const Stmt &stmt, bool isLast);

    void visitBlockItem(const BlockItem &blockItem, bool isLast);

    void visitBlock(const Block &block, bool isFuncBlock);
};
