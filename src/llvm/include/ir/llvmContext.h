#pragma once

#include <unordered_map>

#include "symtable.h"
#include "type.h"
#include "value/Constant.h"
#include "value/Function.h"

/**
 * A single Context can be shared by multiple Modules.
 * The Context stores common types, constants, and functions,
 * ensuring uniqueness and enabling reuse across the IR.
 * But in this toy compiler, we only use one Context and one Module.
 */
class LlvmContext {
public:
    LlvmContext() {
        int_ty_ = std::make_unique<IntegerType>(32);
    }

    ~LlvmContext();

    std::shared_ptr<Type> getIntegerTy() const {
        return int_ty_;
    }

    std::shared_ptr<Type> getArrayTy(std::shared_ptr<Type> elementTy, int num) {
        const auto arrayTy = std::make_shared<ArrayType>(elementTy, num);
        const auto key = std::make_pair(elementTy, num);

        const auto it = arrayTypes_.find(key);
        if (it != arrayTypes_.end()) {
            return it->second;
        }

        arrayTypes_[key] = arrayTy;
        return arrayTy;
    }

    FunctionType &getFunctionTy() const {
        return *func_ty_;
    }

    Constant* getIntConstant(const int value) {
        return intConstants_[value];
    }

    void registerConstant(Constant* constant, const int value) {
        intConstants_[value] = constant;
    }

    void registerFunction(Function* function) {
        functions_[function->getName()] = function;
    }

private:
    // LlvmContext own all types.
    std::shared_ptr<IntegerType> int_ty_;
    std::shared_ptr<FunctionType> func_ty_;

    using ArrayTypeKey = std::pair<std::shared_ptr<Type>, int>;
    std::unordered_map<ArrayTypeKey, std::shared_ptr<ArrayType>> arrayTypes_;

    std::unordered_map<int, Constant*> intConstants_;

    std::unordered_map<std::string, Function*> functions_;
};