#include "llvm/include/asm/AsmWriter.h"

#include <iostream>

const AsmWriter& AsmWriter::push(char ch) const {
    out_ << ch;
    return *this;
}

const AsmWriter& AsmWriter::push(const std::string& str, ...) const {
    out_ << str;
    return *this;
}

const AsmWriter& AsmWriter::pushSpace() const {
    return push(' ');
}

const AsmWriter& AsmWriter::pushLine(const std::string& str, ...) const {
    return push(str).push('\n');
}

const AsmWriter& AsmWriter::pushComment(const std::string& str, ...) const {
    return push("; ").push(str).push('\n');
}
