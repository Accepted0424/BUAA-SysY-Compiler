#pragma once

#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/ConstantData.h"
#include <map>
#include <utility> // for std::pair

class ConstantInt : public ConstantData {
public:
    static ConstantIntPtr create(const TypePtr &type, const int value) {
        // ---------------------------------------------------------
        // 升级版缓存：Key 包含了 (类型指针, 整数值)
        // 这样 i1 的 0 和 i32 的 0 会被存放在不同的位置
        // ---------------------------------------------------------
        static std::map<std::pair<TypePtr, int>, ConstantIntPtr> cache;

        // 1. 构造复合 Key
        std::pair<TypePtr, int> key = {type, value};

        // 2. 查表
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second; // 命中缓存，返回旧对象
        }

        // 3. 创建新对象 (使用 private constructor 的技巧，或者直接 new)
        // 注意：这里建议用 new 然后转 shared_ptr，或者友元类，
        // 为了保持你原有代码风格，这里用简单的 new。
        auto newConst = std::shared_ptr<ConstantInt>(new ConstantInt(type, value));

        // 4. 存入缓存
        cache[key] = newConst;

        return newConst;
    }

    // 构造函数保持不变
    ConstantInt(const TypePtr &type, const int value)
        : ConstantData(ValueType::ConstantIntTy, type), value_(value) {}

    int getValue() const { return value_; }

private:
    int value_;
};