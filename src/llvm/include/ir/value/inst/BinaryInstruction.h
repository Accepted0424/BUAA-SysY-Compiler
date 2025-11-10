#pragma once

#include "Instruction.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/Value.h"

class BinaryInstruction : public Instruction {
public:
    ~BinaryInstruction() override = default;

protected:
    BinaryInstruction(ValueType valueType, TypePtr type, ValuePtr lhs,
                      ValuePtr rhs) : Instruction(valueType, type) {
    };
};

enum class BinaryOpType { Add, Sub, Mul, Div, Mod };

class BinaryOperator final : public BinaryInstruction {
public:
    ~BinaryOperator() override = default;

    BinaryOperator(TypePtr type, ValuePtr lhs, ValuePtr rhs,
                   BinaryOpType opType)
        : BinaryInstruction(ValueType::BinaryOperatorTy, type, lhs, rhs),
          op_type_(opType) {}

    static BinaryOperatorPtr create(BinaryOpType opType, ValuePtr lhs, ValuePtr rhs) {
        auto type = lhs->getType();
        return std::make_shared<BinaryOperator>(type, lhs, rhs, opType);
    }

    BinaryOpType OpType() const { return op_type_; }

private:
    BinaryOpType op_type_;
};
