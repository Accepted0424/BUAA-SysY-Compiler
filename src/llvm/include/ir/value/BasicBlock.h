#pragma once

#include "llvm/include/ir/value/Value.h"
#include "llvm/include/ir/IrForward.h"
#include <list>
#include <memory>

class BasicBlock : public Value, public std::enable_shared_from_this<BasicBlock> {
public:
    ~BasicBlock() override = default;

    static bool classof(const ValueType type) {
        return type == ValueType::BasicBlockTy;
    }

    static BasicBlockPtr create(FunctionPtr parent = nullptr);

    using instruction_iterator = std::list<InstructionPtr>::iterator;

    int InstructionCount() const {
        return static_cast<int>(instructions_.size());
    }

    // Insert an instruction at the end of the basic block.
    BasicBlockPtr insertInstruction(InstructionPtr instruction);

    // Insert an instruction before the specified iterator.
    BasicBlockPtr insertInstruction(instruction_iterator iter,
                                    InstructionPtr inst);

    // Remove an instruction from the basic block.
    BasicBlockPtr removeInstruction(InstructionPtr instruction);

    instruction_iterator instructionBegin() {
        return instructions_.begin();
    }

    instruction_iterator instructionEnd() {
        return instructions_.end();
    }

private:
    BasicBlock(FunctionPtr parent);

    FunctionPtr parent_;

    std::list<InstructionPtr> instructions_;
};
