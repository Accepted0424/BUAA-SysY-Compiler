#pragma once

#include "Instruction.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/Value.h"

class UnaryInstruction : public Instruction {
public:
    ~UnaryInstruction() override = default;

    static bool classof(const ValueType type) {
        return type == ValueType::LoadInstTy ||
               type == ValueType::UnaryOperatorTy;
    }

protected:
    UnaryInstruction(ValueType valueType, TypePtr type, ValuePtr operand)
        : Instruction(valueType, type) {};
};

enum class UnaryOpType { NOT, NEG, POS };

class UnaryOperator final : public UnaryInstruction {
public:
    ~UnaryOperator() override = default;

    UnaryOperator(TypePtr type, ValuePtr operand, UnaryOpType opType):
        UnaryInstruction(ValueType::UnaryOperatorTy, type, operand),
        op_type_(opType) {}

    static UnaryOperatorPtr create(UnaryOpType opType, ValuePtr operand) {
        auto type = operand->getType();
        return std::make_shared<UnaryOperator>(type, operand, opType);
    }

    UnaryOpType OpType() const { return op_type_; }

private:
    UnaryOpType op_type_;
};