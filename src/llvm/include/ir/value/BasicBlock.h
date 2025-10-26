#pragma once

#include "llvm/include/ir/value/Value.h"
#include <list>

#include "llvm/include/ir/HasParent.h"

class BasicBlock : public Value, public HasParent<Function> {
public:
    ~BasicBlock() override = default;

    static bool classof(const ValueType type) {
        return type == ValueType::BasicBlockTy;
    }

    void PrintAsm(AsmWriterPtr out) override;

    void PrintName(AsmWriterPtr out) override;

    void PrintUse(AsmWriterPtr out) override;

    static BasicBlockPtr New(FunctionPtr parent = nullptr);

    using instruction_iterator = std::list<InstructionPtr>::iterator;

    int InstructionCount() const {
        return static_cast<int>(instructions_.size());
    }

    // Insert an instruction at the end of the basic block.
    BasicBlockPtr InsertInstruction(InstructionPtr instruction);

    // Insert an instruction before the specified iterator.
    BasicBlockPtr InsertInstruction(instruction_iterator iter,
                                    InstructionPtr inst);

    // Remove an instruction from the basic block.
    BasicBlockPtr RemoveInstruction(InstructionPtr instruction);

    instruction_iterator InstructionBegin() {
        return instructions_.begin();
    }

    instruction_iterator InstructionEnd() {
        return instructions_.end();
    }

private:
    BasicBlock(FunctionPtr parent);

    std::list<InstructionPtr> instructions_;
};
