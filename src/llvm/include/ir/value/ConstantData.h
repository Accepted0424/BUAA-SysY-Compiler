#pragma once

class ConstantData : public Constant {
public:
    ~ConstantData() override = default;

    ConstantData(TypePtr type)
        : Constant(ValueType::ConstantDataTy, type) {}
};