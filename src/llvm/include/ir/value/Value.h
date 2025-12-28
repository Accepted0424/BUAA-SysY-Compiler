#pragma once

#include <string>
#include <utility>
#include "llvm/include/ir/IrForward.h"

enum class ValueType {
    // Value
    ArgumentTy,
    BasicBlockTy,

    // Value -> Constant
    ConstantTy,
    ConstantDataTy,
    ConstantIntTy,
    ConstantArrayTy,

    // Value -> Constant -> GlobalValue
    FunctionTy,
    GlobalVariableTy,

    // Value -> User -> Instruction
    BinaryOperatorTy,
    CompareInstTy,
    LogicalInstTy,
    ZExtInstTy,
    BranchInstTy,
    JumpInstTy,
    ReturnInstTy,
    StoreInstTy,
    CallInstTy,
    InputInstTy,
    OutputInstTy,
    AllocaInstTy,
    LoadInstTy,
    UnaryOperatorTy,
    GetElementPtrInstTy,
};

class Value {
public:
    // Always use virtual destructor for base class.
    virtual ~Value() = default;

    ValueType getValueType() const {
        return value_type_;
    }

    TypePtr getType() const {
        return type_;
    }

    const std::string &getName() const {
        return name_;
    }

    void setName(const std::string &name) {
        name_ = name;
    }

    void addUse(User *user);

    void removeUse(User *user);

    const UseList &getUses() const {
        return uses_;
    }

    void replaceAllUsesWith(const ValuePtr &newValue);

    int getUseCount() const {
        return static_cast<int>(uses_.size());
    }

protected:
    TypePtr type_;

    std::string name_;

    Value(ValueType valueType, TypePtr type)
        : type_(std::move(type)), value_type_(valueType) {}

    Value(ValueType ValueType, TypePtr type, std::string name)
        : type_(std::move(type)), name_(std::move(name)), value_type_(ValueType) {}

private:
    ValueType value_type_;
    UseList uses_;
};
