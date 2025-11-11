#pragma once

class ConstantData : public Constant {
public:
    ~ConstantData() override = default;

    ConstantData(ValueType valueTy, TypePtr type)
        : Constant(valueTy, type) {}
};