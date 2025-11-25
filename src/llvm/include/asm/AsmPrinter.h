#pragma once
#include "AsmWriter.h"

class AsmPrinter {
public:
    AsmPrinter(Module &module, std::ostream &out): module_(module), out_(out) {
        writer_ = AsmWriter::create(module, out);
    }

    void print() const;

    void printHeader() const;

    void printModule() const;

private:
    Module &module_;

    std::ostream &out_;

    std::unique_ptr<AsmWriter> writer_;
};
