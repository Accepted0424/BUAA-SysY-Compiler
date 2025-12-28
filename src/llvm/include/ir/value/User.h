#pragma once

#include "llvm/include/ir/value/Value.h"
#include <vector>

// User represent a value that has operands.
class User : public Value {
public:
    ~User() override = default;

    User(ValueType valueType, TypePtr type) : Value(valueType, type) {}

    const std::vector<ValuePtr> &getOperands() const { return operands_; }

    virtual void replaceOperandValue(const Value *oldVal, const ValuePtr &newVal) {
        replaceOperand(oldVal, newVal);
    }

protected:
    void addOperand(const ValuePtr &operand) {
        operands_.push_back(operand);
        if (operand) {
            operand->addUse(this);
        }
    }

    bool replaceOperand(const Value *oldVal, const ValuePtr &newVal) {
        bool replaced = false;
        for (auto &op : operands_) {
            if (op.get() == oldVal) {
                if (op) {
                    op->removeUse(this);
                }
                op = newVal;
                if (newVal) {
                    newVal->addUse(this);
                }
                replaced = true;
            }
        }
        return replaced;
    }

private:
    std::vector<ValuePtr> operands_;
};
