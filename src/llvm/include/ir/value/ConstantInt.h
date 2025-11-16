#pragma once

#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/ConstantData.h"

class ConstantInt : public ConstantData {
public:
    static ConstantIntPtr create(const TypePtr &type, const int value) {
        return std::make_shared<ConstantInt>(type, value);
    }

    ConstantInt(const TypePtr &type, const int value)
        : ConstantData(ValueType::ConstantIntTy, type), value_(value) {}

private:
    int value_;
};
