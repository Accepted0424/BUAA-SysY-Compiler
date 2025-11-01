#pragma once

#include "llvm/include/ir/SlotTracker.h"
#include "GlobalValue.h"
#include "BasicBlock.h"

#include <list>

class Function final : public GlobalValue {
public:
    ~Function() override = default;

    BasicBlock* NewBasicBlock();

    static Function* create(LlvmContext &context, Type* returnType, const std::string &name);

    static Function* create(LlvmContext &context, Type* returnType, const std::string &name,
                       std::vector<Argument*> args);

    using block_iterator = std::list<BasicBlock*>::iterator;

    using argument_iterator = std::vector<Argument*>::iterator;

    // Insert a basic block at the end of the function.
    Function* InsertBasicBlock(BasicBlock* block);
    // Insert a basic block before the specified iterator.
    Function* InsertBasicBlock(block_iterator iter, BasicBlock* block);
    // Remove a basic block from the function.
    Function* RemoveBasicBlock(BasicBlock* block);

    block_iterator BasicBlockBegin() { return basicBlocks_.begin(); }

    block_iterator BasicBlockEnd() { return basicBlocks_.end(); }

private:
    Function(Type* type, const std::string &name)
        : GlobalValue(ValueType::FunctionTy, type, name), returnType_(type) {}

    Function(Type* type, const std::string &name, std::vector<Argument*> &args)
        : GlobalValue(ValueType::FunctionTy, type, name), returnType_(type), args_(args) {}

    Type* returnType_;

    std::vector<Argument*> args_;

    std::list<BasicBlock*> basicBlocks_;
};
