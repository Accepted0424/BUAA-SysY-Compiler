#pragma once

#include "llvm/include/ir/HasParent.h"
#include "llvm/include/ir/value/User.h"

class Instruction : public User {
public:
    ~Instruction() override = default;

private:
};
