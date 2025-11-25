#include "include/asm/AsmPrinter.h"

#include "ir/value/Function.h"

void AsmPrinter::print() const {
    printHeader();


}

void AsmPrinter::printHeader() const {
    writer_->pushLine("declare i32 @getint()          ; 读取一个整数")
        .pushLine("declare void @putint(i32)      ; 输出一个整数")
        .pushLine("declare void @putch(i32)       ; 输出一个字符")
        .pushLine("declare void @putstr(i8*)      ; 输出字符串");
}

void AsmPrinter::printModule() const {
    // global variables
    for (auto it = module_.globalVarBegin(); it != module_.globalVarEnd(); ++it) {
        (it*)->PrintAsm(out_);
    }

    // Functions.
    for (auto it = module_.functionBegin(); it != module_.functionEnd(); ++it) {
        (*it)->PrintAsm(out_);
    }

    // Main function.
    if (module_.getMainFunction()) {
        module_.getMainFunction()->PrintAsm(out_);
    }
}