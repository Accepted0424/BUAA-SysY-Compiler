#pragma once

#include "Value.h"
#include "llvm/include/ir/IrForward.h"

class Argument final : public Value {
public:
    ~Argument() override = default;

    Argument(const TypePtr &type, const std::string &name):
        Value(ValueType::ArgumentTy, type), name_(name) {
        setName(name);
    }

    static ArgumentPtr Create(TypePtr type, const std::string &name) {
        return std::make_shared<Argument>(type, name);
    }

private:
    std::string name_;
};
