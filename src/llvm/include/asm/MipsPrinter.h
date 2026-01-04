#pragma once

#include <ostream>

#include "llvm/include/ir/module.h"

class MipsPrinter {
public:
    MipsPrinter(Module &module, std::ostream &out, bool enableOpt = true)
        : module_(module), out_(out), enableOpt_(enableOpt) {}

    void print() const;

private:
    Module &module_;
    std::ostream &out_;
    bool enableOpt_ = true;
};
