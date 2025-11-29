#pragma once

#include <unordered_map>
#include <functional>
#include <utility>

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
        bool_ty_ = std::make_shared<IntegerType>(1);
        void_ty_ = std::make_shared<VoidType>();
    }

    ~LlvmContext() = default;

    TypePtr getVoidTy() {
        return void_ty_;
    }

    TypePtr getIntegerTy() {
        return int_ty_;
    }

    TypePtr getBoolTy() {
        return bool_ty_;
    }

    TypePtr getArrayTy(TypePtr elementTy, int elementNum = -1) {
        const auto key = std::make_pair(elementTy.get(), elementNum);
        auto it = arrayTypes_.find(key);
        if (it != arrayTypes_.end()) {
            return it->second;
        }
        auto arrayType = std::make_shared<ArrayType>(elementTy, elementNum);
        arrayTypes_[key] = arrayType;
        return arrayType;
    }

    Constant* getIntConstant(const int value) {
        return intConstants_[value];
    }

private:
    struct ArrayTypeKeyHash {
        size_t operator()(const std::pair<Type*, int> &key) const {
            return std::hash<Type*>()(key.first) ^ (std::hash<int>()(key.second) << 1);
        }
    };

    // LlvmContext own all types.
    std::shared_ptr<IntegerType> int_ty_;
    std::shared_ptr<IntegerType> bool_ty_;
    std::shared_ptr<VoidType> void_ty_;

    std::unordered_map<std::pair<Type*, int>, TypePtr, ArrayTypeKeyHash> arrayTypes_;

    std::unordered_map<int, Constant*> intConstants_;

    std::unordered_map<std::string, Function*> functions_;
};
