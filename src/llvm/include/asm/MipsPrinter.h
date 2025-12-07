#pragma once

#include <ostream>

#include "llvm/include/ir/module.h"

class MipsPrinter {
public:
    MipsPrinter(Module &module, std::ostream &out)
        : module_(module), out_(out) {}

    void print() const;

private:
    Module &module_;
    std::ostream &out_;
};
