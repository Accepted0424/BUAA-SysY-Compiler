#pragma once

#include "GlobalValue.h"
#include "BasicBlock.h"

#include <list>
#include <vector>

class Function final : public GlobalValue {
public:
    ~Function() override = default;

    Function(const TypePtr &type, const std::string &name)
        : GlobalValue(ValueType::FunctionTy, type, name), returnType_(type) {}

    Function(const TypePtr &type, const std::string &name, const std::vector<ArgumentPtr> &args)
        : GlobalValue(ValueType::FunctionTy, type, name), returnType_(type), args_(args) {}

    static FunctionPtr create(TypePtr returnType, const std::string &name) {
        return std::make_shared<Function>(returnType, name);
    }

    static FunctionPtr create(TypePtr returnType, const std::string &name, std::vector<ArgumentPtr> args) {
        return std::make_shared<Function>(returnType, name, args);
    }

    TypePtr getReturnType() const { return returnType_; }

    std::list<BasicBlockPtr>::iterator basicBlockBegin() {
        return basicBlocks_.begin();
    }

    std::list<BasicBlockPtr>::iterator basicBlockEnd() {
        return basicBlocks_.end();
    }

    void addBasicBlock(BasicBlockPtr bb) {
        basicBlocks_.push_back(std::move(bb));
    }

    void removeBasicBlock(const BasicBlockPtr &bb) {
        basicBlocks_.remove(bb);
    }

    BasicBlockPtr getEntryBlock() {
        if (basicBlocks_.empty()) {
            return nullptr;
        }
        return basicBlocks_.front();
    }

    const std::vector<ArgumentPtr> &getArgs() const {
        return args_;
    }

private:
    TypePtr returnType_;

    std::vector<ArgumentPtr> args_;

    std::list<BasicBlockPtr> basicBlocks_;
};
