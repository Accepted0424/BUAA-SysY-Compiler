#pragma once
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <utility>

#include "error.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/value/Value.h"

enum SymbolType {
    INT,
    INT_ARRAY,
    CONST_INT,
    CONST_INT_ARRAY,
    STATIC_INT,
    STATIC_INT_ARRAY,
    VOID_FUNC,
    INT_FUNC
};

// type到字符串的映射
inline std::string symbolTypeToString(const SymbolType type) {
    switch (type) {
        case INT:
            return "Int";
        case INT_ARRAY:
            return "IntArray";
        case CONST_INT:
            return "ConstInt";
        case CONST_INT_ARRAY:
            return "ConstIntArray";
        case STATIC_INT:
            return "StaticInt";
        case STATIC_INT_ARRAY:
            return "StaticIntArray";
        case VOID_FUNC:
            return "VoidFunc";
        case INT_FUNC:
            return "IntFunc";
        default:
            return "unknown";
    }
}

struct Symbol {
    SymbolType type;
    std::string name;
    int lineno;
    std::shared_ptr<Value> value;

    virtual ~Symbol() = default;

    Symbol(const SymbolType type, std::string name, const std::shared_ptr<Value> &value, const int lineno)
        : type(type), name(std::move(name)), lineno(lineno), value(value) {}
};

struct IntSymbol : Symbol {
    IntSymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(INT, name, value, lineno) {}
};

struct IntArraySymbol : Symbol {
    IntArraySymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(INT_ARRAY, name, value, lineno) {}
};

struct ConstIntSymbol : Symbol {
    ConstIntSymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(CONST_INT, name, value, lineno) {}
};

struct ConstIntArraySymbol : Symbol {
    ConstIntArraySymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(CONST_INT_ARRAY, name, value, lineno) {}
};

struct StaticIntSymbol : Symbol {
    StaticIntSymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(STATIC_INT, name, value, lineno) {}
};

struct StaticIntArraySymbol : Symbol {
    StaticIntArraySymbol(const std::string &name, const std::shared_ptr<Value> &value, const int lineno)
        : Symbol(STATIC_INT_ARRAY, name, value, lineno) {}
};

struct FuncSymbol : Symbol {
    TypePtr returnType;
    std::vector<TypePtr> params;

    FuncSymbol(const SymbolType type, const std::string &name, const std::shared_ptr<Value> &value,
        const std::vector<TypePtr> &params, const int lineno)
        : Symbol(type, name, value, lineno), params(params) {}

    int getParamCnt() const {
        return static_cast<int>(params.size());
    }
};

struct VoidFuncSymbol : FuncSymbol {
    VoidFuncSymbol(const std::string &name, const std::shared_ptr<Value> &value,
        const std::vector<TypePtr> &params, const int lineno)
        : FuncSymbol(VOID_FUNC, name, value, params, lineno) {}
};

struct IntFuncSymbol : FuncSymbol {
    IntFuncSymbol(const std::string &name, const std::shared_ptr<Value> &value,
        const std::vector<TypePtr> &params, const int lineno)
        : FuncSymbol(INT_FUNC, name, value, params, lineno) {}
};

class SymbolTable : public std::enable_shared_from_this<SymbolTable> {
public:
    SymbolTable() : parent_(nullptr), id_(++global_id_) {}

    SymbolTable(std::ofstream &out) : parent_(nullptr), out_(out), id_(++global_id_) {}

    bool existInSymTable(const std::string &name) {
        if (symbols_.find(name) != symbols_.end()) {
            return true;
        }

        if (parent_ == nullptr) {
            return false;
        }

        return parent_->existInSymTable(name);
    }

    bool existInScope(const std::string &name) {
        return symbols_.find(name) != symbols_.end();
    }

    bool addSymbol(const std::shared_ptr<Symbol>& symbol) {
        if (existInScope(symbol->name)) {
            ErrorReporter::error(symbol->lineno, ERR_REDEFINED_NAME);
            return false;
        }

        symbols_[symbol->name] = symbol;
        ordered_.push_back(symbol);

        return true;
    }

    std::shared_ptr<Symbol> getSymbol(const std::string &name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return it->second;
        }
        if (parent_ != nullptr) {
            return parent_->getSymbol(name);
        }
        return nullptr;
    }

    std::shared_ptr<FuncSymbol> getFuncSymbol(const std::string &name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            if (it->second->type == VOID_FUNC || it->second->type == INT_FUNC) {
                return std::static_pointer_cast<FuncSymbol>(it->second);
            }
            return nullptr;
        }
        if (parent_ != nullptr) {
            return parent_->getFuncSymbol(name);
        }
        return nullptr;
    }

    std::shared_ptr<SymbolTable> pushScope() {
        auto newTab = std::make_shared<SymbolTable>();
        newTab->parent_ = shared_from_this();
        children_.push_back(newTab);
        return newTab;
    }

    std::shared_ptr<SymbolTable> popScope() {
        return parent_;
    }

    bool isGlobalScope() const {
        return parent_ == nullptr;
    }

    void printAllScopes() const {
        // 收集所有作用域
        std::vector<std::shared_ptr<const SymbolTable>> allScopes;
        collectScopes(allScopes);

        // 按 id 升序排列
        std::sort(allScopes.begin(), allScopes.end(),
                  [](const auto& a, const auto& b) {
                      return a->id_ < b->id_;
                  });

        // 按序打印
        for (const auto& scope : allScopes) {
            for (const auto& sym : scope->ordered_) {
                std::string typeStr = symbolTypeToString(sym->type);
                out_->get() << scope->id_ << " " << sym->name << " " << typeStr << std::endl;
                std::cout << scope->id_ << " " << sym->name << " " << typeStr << std::endl;
            }
        }
    }

    // 递归收集作用域
    void collectScopes(std::vector<std::shared_ptr<const SymbolTable>>& out) const {
        out.push_back(shared_from_this());
        for (auto& child : children_) {
            child->collectScopes(out);
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols_;

    std::vector<std::shared_ptr<Symbol>> ordered_;

    std::shared_ptr<SymbolTable> parent_;

    std::vector<std::shared_ptr<SymbolTable>> children_;

    std::optional<std::reference_wrapper<std::ofstream>> out_;

    int id_;

    inline static int global_id_ = 0;
};