#include "llvm/include/ir/value/Value.h"

#include "llvm/include/ir/value/Use.h"
#include "llvm/include/ir/value/User.h"

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

void Value::replaceAllUsesWith(const ValuePtr &newValue) {
    if (newValue.get() == this) {
        return;
    }
    UseList usesCopy = uses_;
    for (const auto &use : usesCopy) {
        if (!use) continue;
        auto *user = use->getUser();
        if (user) {
            user->replaceOperandValue(this, newValue);
        }
    }
    uses_.clear();
}
