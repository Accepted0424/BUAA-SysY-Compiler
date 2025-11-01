#pragma once

#include "llvm/asm/AsmWriter.h"
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

protected:
    Type(TypeID typeId)
        : typeId_(typeId) {
    }

private:
    TypeID typeId_;
};

/*
 * Represent an integer. In tolang, it is the only type used
 * in arithmetic operations.
 */
class IntegerType : public Type {
    friend class LlvmContext;

public:
    ~IntegerType() override = default;

protected:
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

protected:
    ArrayType(const std::shared_ptr<Type> &element_type, const int element_num)
        : Type(ArrayTyID), element_type_(element_type), element_num_(element_num) {
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

    void PrintAsm(AsmWriterPtr out) override;

    static FunctionTypePtr Get(TypePtr returnType,
                               const std::vector<Type *> &paramTypes);

    static FunctionTypePtr Get(TypePtr returnType);

    TypePtr ReturnType() const { return _returnType; }
    const std::vector<TypePtr> &ParamTypes() const { return _paramTypes; }

    bool Equals(TypePtr returnType,
                const std::vector<TypePtr> &paramTypes) const;

    bool Equals(TypePtr returnType) const;

private:
    FunctionType(TypePtr returnType, const std::vector<TypePtr> &paramTypes);

    FunctionType(TypePtr returnType);

    TypePtr _returnType;
    std::vector<TypePtr> _paramTypes;
};
