#include "include/asm/AsmPrinter.h"

void AsmPrinter::print() const {
    writer_->push("hello, world")
            .push("hello, world");
}
