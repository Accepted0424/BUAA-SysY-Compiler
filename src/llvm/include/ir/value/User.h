#pragma once

#include "llvm/include/ir/value/Value.h"

// User represent a value that has operands.
class User : public Value {
public:
    ~User() override = default;

    User(ValueType valueType, TypePtr type) : Value(valueType, type) {}
};