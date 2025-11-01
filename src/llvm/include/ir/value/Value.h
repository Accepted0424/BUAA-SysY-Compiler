#pragma once

#include <list>
#include <string>

#include "llvm/include/ir/type.h"

// All types used in LLVM for tolang.
enum class ValueType {
    // Value
    ArgumentTy,
    BasicBlockTy,

    // Value -> Constant
    ConstantTy,
    ConstantDataTy,

    // Value -> Constant -> GlobalValue
    FunctionTy,
    GlobalVariableTy,

    // Value -> User -> Instruction
    BinaryOperatorTy,
    CompareInstTy,
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
};

// Base class for all values in LLVM.
class Value {
public:
    // Always use virtual destructor for base class.
    virtual ~Value() = default;

    ValueType getValueType() const {
        return value_type_;
    }

    std::shared_ptr<Type> getType() const {
        return type_;
    }

    const std::string &getName() const {
        return name_;
    }

    void SetName(const std::string &name) {
        name_ = name;
    }

protected:
    std::shared_ptr<Type> type_;

    std::string name_;

    Value(ValueType valueType, std::shared_ptr<Type> type)
        : type_(type), value_type_(valueType) {}

    Value(ValueType ValueType, std::shared_ptr<Type> type, const std::string &name)
        : type_(type), name_(name), value_type_(ValueType) {}

private:
    ValueType value_type_;
};