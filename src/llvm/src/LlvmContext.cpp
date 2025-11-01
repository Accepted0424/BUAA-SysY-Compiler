#include "llvm/include/ir/llvmContext.h"
#include "llvm/include/ir/value/Constant.h"
#include "llvm/include/ir/value/Function.h"

// Destructor to clean up allocated resources
LlvmContext::~LlvmContext() {
    // Clean up registered functions
    for (auto& [name, func] : functions_) {
        delete func;
    }
    // Clean up integer constants
    for (auto& [name, constant] : intConstants_) {
        delete constant;
    }
}
