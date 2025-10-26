#pragma once

#include "irForward.h"
#include "type.h"
#include <vector>

/// One LLVM context per module, which holds all the types and values.
class LlvmContext {
    // Only Module can create a context.
    friend class Module;

public:
    ~LlvmContext();

    // prevent copying
    LlvmContext(const LlvmContext &) = delete;
    LlvmContext &operator=(const LlvmContext &) = delete;

    // Save all allocated values to avoid memory leak.
    template <typename _Ty> _Ty *SaveValue(_Ty *value) {
        values_.push_back(value);
        return value->template As<_Ty>();
    }

    UsePtr SaveUse(UsePtr use) {
        uses_.push_back(use);
        return use;
    }

private:
    LlvmContext();

    std::vector<ValuePtr> values_;
    std::vector<UsePtr> uses_;
};