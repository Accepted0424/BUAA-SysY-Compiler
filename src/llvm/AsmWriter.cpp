#include "llvm/include/asm/AsmWriter.h"

#include <memory>
#include <iostream>

const AsmWriter& AsmWriter::push(char ch) const {
    out_ << ch;
    return *this;
}

const AsmWriter& AsmWriter::push(std::string str, ...) const {
    out_ << str;
    return *this;
}
