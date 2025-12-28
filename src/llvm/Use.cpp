#include "llvm/include/ir/value/Use.h"

UsePtr Use::New(User *user, Value *value) {
    return std::make_shared<Use>(user, value);
}
