#pragma once

#include <unordered_map>

#include "IrForward.h"
#include "type.h"
#include "value/Constant.h"

/**
 * A single Context can be shared by multiple Modules.
 * The Context stores common types, constants, and functions,
 * ensuring uniqueness and enabling reuse across the IR.
 * But in this toy compiler, we only use one Context and one Module.
 */
class LlvmContext {
public:
    LlvmContext() {
        int_ty_ = std::make_shared<IntegerType>(32);
        void_ty_ = std::make_shared<VoidType>();
    }

    ~LlvmContext() = default;

    TypePtr getVoidTy() {
        return void_ty_;
    }

    TypePtr getIntegerTy() {
        return int_ty_;
    }

    TypePtr getArrayTy(TypePtr elementTy) {
        if (arrayTypes_.find(elementTy->typeId_) != arrayTypes_.end()) {
            return arrayTypes_[elementTy->typeId_];
        }
        auto arrayType = std::make_shared<ArrayType>(elementTy);
        arrayTypes_[elementTy->typeId_] = arrayType;
        return arrayType;
    }

    Constant* getIntConstant(const int value) {
        return intConstants_[value];
    }

private:
    // LlvmContext own all types.
    std::shared_ptr<IntegerType> int_ty_;
    std::shared_ptr<VoidType> void_ty_;

    std::unordered_map<Type::TypeID, TypePtr> arrayTypes_;

    std::unordered_map<int, Constant*> intConstants_;

    std::unordered_map<std::string, Function*> functions_;
};