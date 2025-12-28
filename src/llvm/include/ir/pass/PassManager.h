#pragma once

#include <memory>
#include <vector>

#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/module.h"

class FunctionPass {
public:
    virtual ~FunctionPass() = default;
    virtual bool run(const FunctionPtr &func, Module &module) = 0;
};

class PassManager {
public:
    void addPass(std::unique_ptr<FunctionPass> pass) {
        passes_.push_back(std::move(pass));
    }

    void run(Module &module);

private:
    std::vector<std::unique_ptr<FunctionPass>> passes_;
};

void addDefaultPasses(PassManager &pm);
