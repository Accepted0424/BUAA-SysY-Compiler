#include "../include/ir/value/inst/Instruction.h"

AllocaInstPtr AllocaInst::New(TypePtr type) {
    return type->Context()->SaveValue(new AllocaInst(type));
}