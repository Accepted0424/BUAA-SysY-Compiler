#pragma once

#include "Instruction.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/Value.h"

class BinaryInstruction : public Instruction {
public:
    ~BinaryInstruction() override = default;

    BinaryInstruction(ValueType valueType, TypePtr type, ValuePtr lhs,
                      ValuePtr rhs) : Instruction(valueType, type), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {
        addOperand(lhs_);
        addOperand(rhs_);
    };

    ValuePtr lhs_;
    ValuePtr rhs_;
};

enum class BinaryOpType { ADD, SUB, MUL, DIV, MOD };

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

enum class CompareOpType { EQL, NEQ, LSS, GRE, LEQ, GEQ };

class CompareOperator final : public BinaryInstruction {
public:
    ~CompareOperator() override = default;

    CompareOperator(TypePtr type, ValuePtr lhs, ValuePtr rhs, CompareOpType opType)
        : BinaryInstruction(ValueType::CompareInstTy, type, lhs, rhs),
          op_type_(opType) {}

    static CompareOperatorPtr create(CompareOpType opType, ValuePtr lhs, ValuePtr rhs) {
        auto type = std::make_shared<IntegerType>(1);
        return std::make_shared<CompareOperator>(type, lhs, rhs, opType);
    }

    ValuePtr getLhs() const { return lhs_; }
    ValuePtr getRhs() const { return rhs_; }
    CompareOpType OpType() const { return op_type_; }

private:
    CompareOpType op_type_;
};

enum class LogicalOpType {
    AND,
    OR
};

class LogicalOperator final : public BinaryInstruction {
public:
    ~LogicalOperator() override = default;

    LogicalOperator(TypePtr type, ValuePtr lhs, ValuePtr rhs, LogicalOpType opType):
        BinaryInstruction(ValueType::LogicalInstTy, type, lhs, rhs),
        op_type_(opType) {}

    static LogicalOperatorPtr create(LogicalOpType opType, ValuePtr lhs, ValuePtr rhs) {
        auto type = lhs->getType();
        return std::make_shared<LogicalOperator>(type, lhs, rhs, opType);
    }

    ValuePtr getLhs() const { return lhs_; }
    ValuePtr getRhs() const { return rhs_; }
    LogicalOpType OpType() const { return op_type_; }

private:
    LogicalOpType op_type_;
};
