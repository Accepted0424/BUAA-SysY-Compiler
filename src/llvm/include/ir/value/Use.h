#pragma once

#include "llvm/include/ir/IrForward.h"

class Use {
public:
    Use(const Use &) = delete;
    Use &operator=(const Use &) = delete;

    // static factory method
    static UsePtr New(UserPtr user, ValuePtr value);

    ValuePtr getValue() const { return value_; }

    UserPtr getUser() const { return user_; }

private:
    UserPtr user_;

    ValuePtr value_;

    Use(UserPtr user, ValuePtr value) : user_(user), value_(value) {}
};