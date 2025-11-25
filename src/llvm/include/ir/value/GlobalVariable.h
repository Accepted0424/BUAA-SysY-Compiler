#include "GlobalValue.h"

class GlobalVariable : public GlobalValue {
public:
    ~GlobalVariable() override =default;

    static GlobalVariablePtr create(const TypePtr &type, const std::string &name, const ValuePtr &value, const bool isConst) {;
        return std::shared_ptr<GlobalVariable>(new GlobalVariable(type, name, value, isConst));
    }

    GlobalVariable(const TypePtr &type, const std::string &name, const ValuePtr &value, const bool isConst)
        : GlobalValue(ValueType::GlobalVariableTy, type, name), value_(value), isConst_(isConst) {}

    ValuePtr value_;

    bool isConst_;
};
