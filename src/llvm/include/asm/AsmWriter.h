#pragma once
#include "llvm/include/ir/module.h"

class AsmWriter;
using AsmWriterPtr = std::unique_ptr<AsmWriter>;

class AsmWriter {
public:
    AsmWriterPtr static create(Module &module, std::ostream &out) {
        return std::make_unique<AsmWriter>(module, out);
    }

    const AsmWriter& push(char ch) const;

    const AsmWriter& push(std::string str, ...) const;

    AsmWriter(Module &module, std::ostream &out) : module_(module), out_(out) {}

private:
    Module &module_;

    std::ostream &out_;
};
