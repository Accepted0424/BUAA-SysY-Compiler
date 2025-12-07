#include "llvm/include/ir/value/BasicBlock.h"
#include "llvm/include/ir/value/Function.h"
#include "llvm/include/ir/value/inst/Instruction.h"

BasicBlock::BasicBlock(FunctionPtr parent)
    : Value(ValueType::BasicBlockTy, nullptr), parent_(std::move(parent)) {}

BasicBlockPtr BasicBlock::create(FunctionPtr parent) {
    auto bb = std::shared_ptr<BasicBlock>(new BasicBlock(parent));
    if (parent) {
        parent->addBasicBlock(bb);
    }
    return bb;
}

BasicBlockPtr BasicBlock::insertInstruction(InstructionPtr instruction) {
    instructions_.push_back(std::move(instruction));
    return shared_from_this();
}

BasicBlockPtr BasicBlock::insertInstruction(instruction_iterator iter,
    InstructionPtr inst) {
    instructions_.insert(iter, std::move(inst));
    return shared_from_this();
}

BasicBlockPtr BasicBlock::removeInstruction(InstructionPtr instruction) {
    instructions_.remove(instruction);
    return shared_from_this();
}
