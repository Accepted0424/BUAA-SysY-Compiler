#pragma once

#include "llvm/include/ir/value/User.h"

class Instruction : public User {
public:
    ~Instruction() override = default;

    Instruction(ValueType valueType, TypePtr type) : User(valueType, type) {}
};

// an instruction to allocate memory on the stack
class AllocaInst : public Instruction {
public:
    ~AllocaInst() override = default;

    static std::shared_ptr<AllocaInst> create(std::shared_ptr<Type> type) {
        return std::make_shared<AllocaInst>(type);
    }

    AllocaInst(std::shared_ptr<Type> type)
        : Instruction(ValueType::AllocaInstTy, type) {};
};

class StoreInst : public Instruction {
public:
    ~StoreInst() override = default;

    static std::shared_ptr<StoreInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> value, std::shared_ptr<Value> address);

    StoreInst();
};

class LoadInst : public Instruction {
public:
    ~LoadInst() override = default;

    static std::shared_ptr<LoadInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> address);

    LoadInst();
};

class CallInst : public Instruction {
public:
    ~CallInst() override = default;

    static std::shared_ptr<CallInst> create(std::shared_ptr<Function> function,
        const std::vector<std::shared_ptr<Value>> &args) {
        return std::make_shared<CallInst>(function, args);
    }

    CallInst(std::shared_ptr<Function> function,
        const std::vector<std::shared_ptr<Value>> &args):
    Instruction(ValueType::CallInstTy, function->getReturnType()) {};
};

class GetElementPtrInst : public Instruction {
public:
    ~GetElementPtrInst() override = default;

    // TODO
    static std::shared_ptr<GetElementPtrInst> create(TypePtr elementType) {
        return std::make_shared<GetElementPtrInst>(elementType);
    }

    GetElementPtrInst(TypePtr elementType)
        : Instruction(ValueType::GetElementPtrInstTy, elementType) {}
};