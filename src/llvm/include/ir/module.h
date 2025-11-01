#pragma once

#include <string>

#include "llvmContext.h"

class Module final {
public:
    Module(const std::string &name, LlvmContext &context)
        : name_(name), context_(&context) {}

    LlvmContext* getContext() const {
        return context_;
    }

    void addMainFunction(Function &function);

private:
    std::string name_;

    LlvmContext* context_;

    std::vector<GlobalValuePtr> global_vars_;

    std::vector<FunctionPtr> functions_;

    FunctionPtr mainFunction_ = nullptr;
};
