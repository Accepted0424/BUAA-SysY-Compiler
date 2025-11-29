#pragma once

#include "llvm/include/ir/value/User.h"
#include "llvm/include/ir/IrForward.h"
#include <vector>

class Instruction : public User {
public:
    ~Instruction() override = default;

    Instruction(ValueType valueType, TypePtr type) : User(valueType, type) {}

    const std::vector<ValuePtr> &getOperands() const { return operands_; }

protected:
    void addOperand(const ValuePtr &operand) { operands_.push_back(operand); }

private:
    std::vector<ValuePtr> operands_;
};

// an instruction to allocate memory on the stack
class AllocaInst : public Instruction {
public:
    ~AllocaInst() override = default;

    static std::shared_ptr<AllocaInst> create(std::shared_ptr<Type> type, const std::string &name = "") {
        auto inst = std::make_shared<AllocaInst>(type);
        inst->setName(name);
        return inst;
    }

    AllocaInst(std::shared_ptr<Type> type)
        : Instruction(ValueType::AllocaInstTy, type) {};
};

class StoreInst : public Instruction {
public:
    ~StoreInst() override = default;

    static std::shared_ptr<StoreInst> create(const ValuePtr &value, const ValuePtr &address) {
        return std::make_shared<StoreInst>(value, address);
    }

    StoreInst(ValuePtr value, ValuePtr address)
        : Instruction(ValueType::StoreInstTy, nullptr), value_(std::move(value)), address_(std::move(address)) {}

    ValuePtr getValueOperand() const { return value_; }
    ValuePtr getAddressOperand() const { return address_; }

private:
    ValuePtr value_;
    ValuePtr address_;
};

class LoadInst : public Instruction {
public:
    ~LoadInst() override = default;

    static std::shared_ptr<LoadInst> create(std::shared_ptr<Type> type,
        std::shared_ptr<Value> address, const std::string &name = "") {
        auto inst = std::make_shared<LoadInst>(type, address);
        inst->setName(name);
        return inst;
    }

    LoadInst(TypePtr type, ValuePtr address)
        : Instruction(ValueType::LoadInstTy, std::move(type)), address_(std::move(address)) {
        addOperand(address_);
    }

    ValuePtr getAddressOperand() const { return address_; }

private:
    ValuePtr address_;
};

class CallInst : public Instruction {
public:
    ~CallInst() override = default;

    static std::shared_ptr<CallInst> create(std::shared_ptr<Function> function,
        const std::vector<std::shared_ptr<Value>> &args, const std::string &name = "") {
        auto inst = std::make_shared<CallInst>(function, args);
        inst->setName(name);
        return inst;
    }

    CallInst(std::shared_ptr<Function> function,
        const std::vector<std::shared_ptr<Value>> &args):
    Instruction(ValueType::CallInstTy, function->getReturnType()), function_(std::move(function)), args_(args) {
        for (const auto &arg : args_) {
            addOperand(arg);
        }
    };

    std::shared_ptr<Function> getFunction() const { return function_; }
    const std::vector<ValuePtr> &getArgs() const { return args_; }

private:
    std::shared_ptr<Function> function_;
    std::vector<ValuePtr> args_;
};

class GetElementPtrInst : public Instruction {
public:
    ~GetElementPtrInst() override = default;

    static std::shared_ptr<GetElementPtrInst> create(TypePtr elementType, ValuePtr address,
        const std::vector<ValuePtr> &indices, const std::string &name = "") {
        auto inst = std::make_shared<GetElementPtrInst>(elementType, address, indices);
        inst->setName(name);
        return inst;
    }

    GetElementPtrInst(TypePtr elementType, ValuePtr address, std::vector<ValuePtr> indices)
        : Instruction(ValueType::GetElementPtrInstTy, elementType), address_(std::move(address)), indices_(std::move(indices)) {
        addOperand(address_);
        for (const auto &idx : indices_) {
            addOperand(idx);
        }
    }

    ValuePtr getAddressOperand() const { return address_; }
    const std::vector<ValuePtr> &getIndices() const { return indices_; }

private:
    ValuePtr address_;
    std::vector<ValuePtr> indices_;
};

class ReturnInst : public Instruction {
public:
    ~ReturnInst() override = default;

    static std::shared_ptr<ReturnInst> create(ValuePtr value = nullptr) {
        return std::make_shared<ReturnInst>(value);
    }

    ReturnInst(ValuePtr value) : Instruction(ValueType::ReturnInstTy, nullptr), value_(std::move(value)) {}

    ValuePtr getReturnValue() const { return value_; }

private:
    ValuePtr value_;
};

class JumpInst : public Instruction {
public:
    ~JumpInst() override = default;

    static std::shared_ptr<JumpInst> create(BasicBlockPtr target) {
        return std::make_shared<JumpInst>(target);
    }

    explicit JumpInst(BasicBlockPtr target)
        : Instruction(ValueType::JumpInstTy, nullptr), target_(std::move(target)) {}

    BasicBlockPtr getTarget() const { return target_; }

private:
    BasicBlockPtr target_;
};

class BranchInst : public Instruction {
public:
    ~BranchInst() override = default;

    static std::shared_ptr<BranchInst> create(ValuePtr cond, BasicBlockPtr trueBB, BasicBlockPtr falseBB) {
        return std::make_shared<BranchInst>(cond, trueBB, falseBB);
    }

    BranchInst(ValuePtr cond, BasicBlockPtr trueBB, BasicBlockPtr falseBB)
        : Instruction(ValueType::BranchInstTy, nullptr),
          cond_(std::move(cond)), true_(std::move(trueBB)), false_(std::move(falseBB)) {
        addOperand(cond_);
    }

    ValuePtr getCondition() const { return cond_; }
    BasicBlockPtr getTrueBlock() const { return true_; }
    BasicBlockPtr getFalseBlock() const { return false_; }

private:
    ValuePtr cond_;
    BasicBlockPtr true_;
    BasicBlockPtr false_;
};

class ZExtInst : public Instruction {
public:
    ~ZExtInst() override = default;

    static std::shared_ptr<ZExtInst> create(TypePtr targetType, ValuePtr operand, const std::string &name = "") {
        auto inst = std::make_shared<ZExtInst>(targetType, operand);
        inst->setName(name);
        return inst;
    }

    ZExtInst(TypePtr targetType, ValuePtr operand)
        : Instruction(ValueType::ZExtInstTy, std::move(targetType)), operand_(std::move(operand)) {
        addOperand(operand_);
    }

    ValuePtr getOperand() const { return operand_; }

private:
    ValuePtr operand_;
};
