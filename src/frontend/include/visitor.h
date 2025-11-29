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

    BasicBlockPtr cur_block_ = nullptr;

    bool inForLoop_ = false;

    std::vector<BasicBlockPtr> breakTargets_;
    std::vector<BasicBlockPtr> continueTargets_;

    BasicBlockPtr entry_block_ = nullptr;
    int blockId_ = 0;

    ValuePtr getLValAddress(const LVal &lval);

    ValuePtr loadIfPointer(const ValuePtr &value);

    ValuePtr toBool(const ValuePtr &value);

    void insertInst(const InstructionPtr &inst, bool toEntry = false);

    BasicBlockPtr newBlock(const std::string &hint);

    FunctionPtr visitFuncDef(const FuncDef &node);

    FunctionPtr visitMainFuncDef(const MainFuncDef &mainFunc);

    ValuePtr visitPrimaryExp(const PrimaryExp &primaryExp);

    ValuePtr visitUnaryExp(const UnaryExp &unaryExp);

    ValuePtr visitMulExp(const MulExp &mulExp);

    ValuePtr visitAddExp(const AddExp &addExp);

    ConstantIntPtr visitConstExp(const ConstExp &constExp);

    ValuePtr visitExp(const Exp &exp);

    void visitConstDecl(const ConstDecl &constDecl);

    void visitVarDecl(const VarDecl &varDecl);

    void visitDecl(const Decl &decl);

    ValuePtr visitRelExp(const RelExp &relExp);

    ValuePtr visitEqExp(const EqExp &eqExp);

    ValuePtr visitLAndExp(const LAndExp &lAndExp);

    ValuePtr visitLOrExp(const LOrExp &lOrExp);

    ValuePtr visitCond(const Cond &cond);

    void visitForStmt(const ForStmt &forStmt);

    bool visitStmt(const Stmt &stmt, bool isLast);

    bool visitBlockItem(const BlockItem &blockItem, bool isLast);

    void visitBlock(const Block &block, bool isFuncBlock);
};
