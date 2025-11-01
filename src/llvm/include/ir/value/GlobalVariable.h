#include "GlobalValue.h"

class GlobalVariable : public GlobalValue {
public:
    ~GlobalVariable() override =default;

    static std::shared_ptr<GlobalVariable> create(const std::shared_ptr<Type> &type, const std::string &name, const bool isConst) {;
        return std::shared_ptr<GlobalVariable>(new GlobalVariable(type, name, isConst));
    }

private:
    GlobalVariable(std::shared_ptr<Type> type, const std::string &name, const bool isConst)
        : GlobalValue(ValueType::GlobalVariableTy, type, name), value(0), isConst(isConst) {}

    int value;

    bool isConst;
};
