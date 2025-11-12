#pragma once

#include <string>
#include <utility>

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

    void SetName(const std::string &name) {
        name_ = name;
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
};