#pragma once

#include <string>

#include "llvmContext.h"
#include "logger.h"

class Module final {
public:
    Module(const std::string &name, LlvmContext &context)
        : name_(name), context_(&context) {}

    LlvmContext* getContext() const {
        return context_;
    }

    FunctionPtr getMainFunction() const {
        return mainFunction_;
    }

    std::vector<GlobalValuePtr>::iterator globalVarBegin() {
        return global_vars_.begin();
    }

    std::vector<GlobalValuePtr>::iterator globalVarEnd() {
        return global_vars_.end();
    }

    std::vector<FunctionPtr>::iterator functionBegin() {
        return functions_.begin();
    }

    std::vector<FunctionPtr>::iterator functionEnd() {
        return functions_.end();
    }

    void addGlobalVar(GlobalValuePtr globalVar) {
        global_vars_.push_back(globalVar);
    }

    void setMainFunction(FunctionPtr mainFunc) {
        if (mainFunction_ == nullptr) {
            mainFunction_ = mainFunc;
        } else {
            LOG_ERROR("only one main function is allowed");
        }
    }

private:
    std::string name_;

    LlvmContext* context_;

    std::vector<GlobalValuePtr> global_vars_;

    std::vector<FunctionPtr> functions_;

    FunctionPtr mainFunction_ = nullptr;
};
