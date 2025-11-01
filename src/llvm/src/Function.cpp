#include "llvm/include/ir/value/Function.h"

Function* Function::create(LlvmContext &context, Type* returnType, const std::string &name) {
    auto* func = new Function(returnType, name);
    context.registerFunction(func);
    return func;
}


Function* Function::create(LlvmContext &context,
                           Type* returnType, const std::string &name, std::vector<Argument*> args) {
    auto function = std::make_unique<Function>(returnType, name, args);
    auto ptr = function.get();
    context.registerFunction(std::move(function));
    return ptr;
}


