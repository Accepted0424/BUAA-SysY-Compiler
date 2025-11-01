#pragma once

#include "llvm/include/ir/HasParent.h"
#include "llvm/include/ir/value/User.h"

class Instruction : public User {
public:
    ~Instruction() override = default;
};

// an instruction to allocate memory on the stack
class AllocaInst : public Instruction {
public:
    ~AllocaInst() override = default;

    static std::shared_ptr<AllocaInst> create(std::shared_ptr<Type> type);

private:
    AllocaInst();
};

class StoreInst : public Instruction {
public:
    ~StoreInst() override = default;

    static std::shared_ptr<StoreInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> value, std::shared_ptr<Value> address);

private:
    StoreInst();
};

class LoadInst : public Instruction {
    ~LoadInst() override = default;

    static std::shared_ptr<LoadInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> address);

private:
    LoadInst();
};

class AddInst : public Instruction {
    ~AddInst() override = default;

    static std::shared_ptr<AddInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> lhs, std::shared_ptr<Value> rhs);

private:
    AddInst(const std::shared_ptr<Value> &lhs, const std::shared_ptr<Value> &rhs)
        : Instruction(), lhs(lhs), rhs(rhs) {}

    std::shared_ptr<Value> lhs;
    std::shared_ptr<Value> rhs;
}
