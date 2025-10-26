# pragma once

#include <memory>

#include "ast.h"
#include "llvm/include/ir/module.h"
#include "symtable.h"

class Visitor {
public:
    Visitor(ModulePtr module)
        : ir_module_(module),
          cur_scope_(std::make_shared<SymbolTable>(nullptr)) {}

    void visit(const AstNode::CompUnit &node);

private:
    ModulePtr ir_module_;

    std::shared_ptr<SymbolTable> cur_scope_;

    FunctionPtr cur_func_ = nullptr;

    BasicBlockPtr cur_block_ = nullptr;

    void visitFuncDef(const AstNode::FuncDef &node);

    ValuePtr visitExp(const AstNode::Exp &exp);

    void visitDecl(const AstNode::Decl &elm);
};
