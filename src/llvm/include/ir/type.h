#pragma once

#include <utility>

#include "llvm/include/ir/value/Value.h"
/*
 * Base class for types in LLVM. Can only be accessed by LlvmContext.
 */
class Type {
    friend class LlvmContext;

public:
    enum TypeID {
        // Primitive types
        VoidTyID,
        LabelTyID,
        // Derived types
        ArrayTyID,
        IntegerTyID,
        FloatTyID,
        FunctionTyID,
        PointerTyID
    };

    // Always use virtual destructor for base class.
    virtual ~Type() = default;

    bool is(TypeID id) const {
        return typeId_ == id;
    }

protected:
    Type(TypeID typeId)
        : typeId_(typeId) {
    }

private:
    TypeID typeId_;
};

class VoidType : public Type {
    friend class LlvmContext;

public:
    ~VoidType() override = default;

    VoidType() : Type(VoidTyID) {}
};

/*
 * Represent an integer. In tolang, it is the only type used
 * in arithmetic operations.
 */
class IntegerType : public Type {
    friend class LlvmContext;

public:
    ~IntegerType() override = default;

    IntegerType(unsigned bitWidth)
        : Type(IntegerTyID), _bitWidth(bitWidth) {
    }

private:
    unsigned _bitWidth;
};

/*
 * Represent an array type.
 */
class ArrayType : public Type {
    friend class LlvmContext;

public:
    ~ArrayType() override = default;

    ArrayType(const std::shared_ptr<Type> &element_type, const int element_num)
        : Type(ArrayTyID), element_type_(element_type), element_num_(element_num) {
    }

    ArrayType(const std::shared_ptr<Type> &element_type)
        : Type(ArrayTyID), element_type_(element_type), element_num_(-1) {
    }

    TypePtr getElementType() const {
        return element_type_;
    }

    int getElementNum() const {
        return element_num_;
    }

private:
    std::shared_ptr<Type> element_type_;
    int element_num_;
};

/*
 * A function's type consists of a return type and a list of parameter
 * types.
 */
class FunctionType : public Type {
    friend class LlvmContext;

public:
    ~FunctionType() override = default;

    TypePtr ReturnType() const { return _returnType; }

    const std::vector<TypePtr> &ParamTypes() const { return _paramTypes; }

private:
    FunctionType(TypePtr returnType, const std::vector<TypePtr> &paramTypes);

    FunctionType(TypePtr returnType);

    TypePtr _returnType;
    std::vector<TypePtr> _paramTypes;
};
