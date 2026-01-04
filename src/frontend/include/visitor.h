# pragma once

#include <memory>
#include <unordered_map>

#include "ast.h"
#include "symtable.h"
#include "llvm/include/ir/module.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/Value.h"
#include "llvm/include/ir/value/inst/BinaryInstruction.h"
#include "llvm/include/ir/value/inst/UnaryInstruction.h"

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

    std::vector<BasicBlockPtr> breakTargets_;
    std::vector<BasicBlockPtr> continueTargets_;

    BasicBlockPtr entry_block_ = nullptr;
    int blockId_ = 0;
    int staticLocalId_ = 0;

    ValuePtr getLValAddress(const LVal &lval);

    ValuePtr loadIfPointer(const ValuePtr &value);

    ValuePtr toBool(const ValuePtr &value);

    ValuePtr zextToInt32(const ValuePtr &value);

    ValuePtr createCmp(CompareOpType op, ValuePtr lhs, ValuePtr rhs);

    std::optional<int> evalConstExpValue(const Exp &exp);
    std::optional<int> evalConstAddWithLVal(const AddExp &addExp);
    std::optional<int> evalConstConstExp(const ConstExp &constExp);
    std::optional<int> constValueOfLVal(const LVal &lval);

    void insertInst(const InstructionPtr &inst, bool toEntry = false);
    void runDCE(const FunctionPtr &func);

    void invalidateLoadCache(const ValuePtr &address);
    void clearLoadCache();

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

    void emitCondBranch(const Cond &cond, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB);
    void emitLOrBranch(const LOrExp &lOrExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB);
    void emitLAndBranch(const LAndExp &lAndExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB);
    void emitEqBranch(const EqExp &eqExp, const BasicBlockPtr &trueBB, const BasicBlockPtr &falseBB);

    void visitForStmt(const ForStmt &forStmt);

    bool visitStmt(const Stmt &stmt);

    bool visitBlockItem(const BlockItem &blockItem, bool isLast);

    void visitBlock(const Block &block, bool isFuncBlock);

    struct CseResult {
        ValuePtr value;
        bool created;
    };

    std::unordered_map<std::string, ValuePtr> &currentCseTable();
    CseResult reuseBinary(BinaryOpType op, ValuePtr lhs, ValuePtr rhs);
    CseResult reuseCompare(CompareOpType op, ValuePtr lhs, ValuePtr rhs);
    CseResult reuseUnary(UnaryOpType op, ValuePtr operand);
    CseResult reuseZExt(TypePtr targetType, ValuePtr operand);
    CseResult reuseGEP(TypePtr elementType, ValuePtr address, const std::vector<ValuePtr> &indices);

    std::unordered_map<BasicBlock*, std::unordered_map<std::string, ValuePtr>> cseTables_;
    std::unordered_map<BasicBlock*, std::unordered_map<const Value*, ValuePtr>> loadCaches_;
};
