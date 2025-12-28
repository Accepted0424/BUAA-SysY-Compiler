#pragma once

#include "llvm/include/ir/IrForward.h"

class Use {
public:
    Use(const Use &) = delete;
    Use &operator=(const Use &) = delete;

    // static factory method
    static UsePtr New(User *user, Value *value);

    Value *getValue() const { return value_; }

    User *getUser() const { return user_; }

    User *user_;

    Value *value_;

    Use(User *user, Value *value) : user_(user), value_(value) {}
};
