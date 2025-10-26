#include "llvm/include/ir/module.h"

ModulePtr Module::New(const std::string &name) {
    return std::shared_ptr<Module>(new Module(name));
}