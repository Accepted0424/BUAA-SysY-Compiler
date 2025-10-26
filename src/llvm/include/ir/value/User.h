#pragma once

#include "llvm/include/ir/value/Value.h"

// User represent a value that has operands.
class User : public Value {
public:
    ~User() override = default;

    static bool classof(ValueType type) {
        return type >= ValueType::BinaryOperatorTy;
    }

    void AddOperand(ValuePtr value);

    ValuePtr RemoveOperand(ValuePtr value);

    ValuePtr ReplaceOperand(ValuePtr oldValue, ValuePtr newValue);

    ValuePtr OperandAt(int index);

    ValuePtr OperandAt(int index) const;

    int OperandCount() const;

    use_iterator UseBegin() {
        return use_list_.begin();
    }

    use_iterator UseEnd() {
        return use_list_.end();
    }

    UseListPtr GetUseList() {
        return &use_list_;
    }

protected:
    UseList use_list_;

    void AddUse(ValuePtr use);

    ValuePtr RemoveUse(ValuePtr use);

    ValuePtr ReplaceUse(ValuePtr oldValue, ValuePtr newValue);

    User(ValueType valueType, TypePtr type) : Value(valueType, type) {}
};