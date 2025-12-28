#pragma once

#include "llvm/include/ir/value/Value.h"
#include <vector>

// User represent a value that has operands.
class User : public Value {
public:
    ~User() override = default;

    User(ValueType valueType, TypePtr type) : Value(valueType, type) {}

    const std::vector<ValuePtr> &getOperands() const { return operands_; }

protected:
    void addOperand(const ValuePtr &operand) {
        operands_.push_back(operand);
        if (operand) {
            operand->addUse(this);
        }
    }

private:
    std::vector<ValuePtr> operands_;
};
