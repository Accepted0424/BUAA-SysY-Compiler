#pragma once

#include "asmWriter.h"
#include "llvm/include/ir/irForward.h"
#include "llvm/include/ir/value/Use.h"
#include "llvm/include/ir/Type.h"
#include <string>

using ValuePtr = Value *;

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

    // This function is used for RTTI (RunTime Type Identification).
    static bool classof(ValueType type) {
        return true;
    }

    // Check if the value is a specific type.
    template <typename _Ty> bool Is() const {
        return _Ty::classof(value_type_);
    }

    /*
     * Cast this type to a specific type.
     * You should use this function only when you are sure that this type is
     * actually the type you want to cast to.
     */
    template <typename _Ty> _Ty *As() {
        return static_cast<_Ty *>(this);
    }

    /*
     * Print the complete asm code of this value. This is used to print
     * the value itself on its occurrence.
     */
    virtual void PrintAsm(AsmWriterPtr out);

    /*
     * Print only the name of this value. For example, only %1.
     */
    virtual void PrintName(AsmWriterPtr out);

    /*
     * Print the use of this value. Usually, the type and name.
     */
    virtual void PrintUse(AsmWriterPtr out);

    ValueType GetValueType() const {
        return value_type_;
    }

    TypePtr GetType() const {
        return type_;
    }

    LlvmContextPtr Context() const {
        return GetType()->Context();
    }

    const std::string &GetName() const {
        return name_;
    }

    void SetName(const std::string &name) {
        name_ = name;
    }

    using use_iterator = UseList::iterator;

    use_iterator UserBegin() {
        return userList_.begin();
    }

    use_iterator UserEnd() {
        return userList_.end();
    }

    UseListPtr GetUserList() {
        return &userList_;
    }

protected:
    TypePtr type_;

    std::string name_;

    UseList userList_;

    void AddUser(UserPtr user);

    UserPtr RemoveUser(UserPtr user);

    Value(ValueType valueType, TypePtr type)
        : type_(type), value_type_(valueType) {}

    Value(ValueType ValueType, TypePtr type, const std::string &name)
        : type_(type), name_(name), value_type_(ValueType) {}

private:
    ValueType value_type_;
};