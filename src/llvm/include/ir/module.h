#pragma once

#include <string>

#include "llvmContext.h"

class Module final {
public:
    // static factory method to create a new Module instance
    static ModulePtr New(const std::string &name);

private:
    Module(const std::string &name);

    std::string name_;
    LlvmContext context_;

    // These will be managed by LlvmContext. So we don't need to delete them.
    std::vector<FunctionPtr> functions_;
    FunctionPtr mainFunction_ = nullptr;
};
