#include "llvm/include/ir/value/Value.h"

#include "llvm/include/ir/value/Use.h"

void Value::addUse(User *user) {
    if (!user) {
        return;
    }
    uses_.push_back(Use::New(user, this));
}

void Value::removeUse(User *user) {
    if (!user) {
        return;
    }
    for (auto it = uses_.begin(); it != uses_.end();) {
        if ((*it)->getUser() == user) {
            it = uses_.erase(it);
        } else {
            ++it;
        }
    }
}
