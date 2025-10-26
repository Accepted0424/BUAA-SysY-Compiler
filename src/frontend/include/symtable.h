#pragma once
#include <unordered_map>

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

struct Symbol {
    SymbolType type;
    std::string name;
    int lineno;
    ValuePtr value;

    virtual ~Symbol() = default;

    Symbol(SymbolType type, std::string name, ValuePtr value, int lineno)
        : type(type), name(name), lineno(lineno), value(value) {}
};

struct IntSymbol : Symbol {
    IntSymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(INT, name, value, lineno) {}
};

struct IntArraySymbol : Symbol {
    IntArraySymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(INT_ARRAY, name, value, lineno) {}
};

struct ConstIntSymbol : Symbol {
    ConstIntSymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(CONST_INT, name, value, lineno) {}
};

struct ConstIntArraySymbol : Symbol {
    ConstIntArraySymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(CONST_INT_ARRAY, name, value, lineno) {}
};

struct StaticIntSymbol : Symbol {
    StaticIntSymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(STATIC_INT, name, value, lineno) {}
};

struct StaticIntArraySymbol : Symbol {
    StaticIntArraySymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(STATIC_INT_ARRAY, name, value, lineno) {}
};

struct VoidFuncSymbol : Symbol {
    // TODO
    VoidFuncSymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(VOID_FUNC, name, value, lineno) {}
};

struct IntFuncSymbol : Symbol {
    // TODO
    IntFuncSymbol(std::string name, ValuePtr value, int lineno)
        : Symbol(INT_FUNC, name, value, lineno) {}
};

class SymbolTable : public std::enable_shared_from_this<SymbolTable> {
public:
    SymbolTable() : parent_(nullptr) {}

    bool existInScope(const std::string &name) {
        return symbols_.find(name) != symbols_.end();
    }

    bool addSymbol(std::shared_ptr<Symbol> symbol) {
        if (existInScope(symbol->name)) {
            return false;
        }

        symbols_[symbol->name] = symbol;
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

    std::shared_ptr<SymbolTable> pushScope() {
        auto newTab = std::make_shared<SymbolTable>();
        newTab->parent_ = shared_from_this();
        return newTab;
    }

    std::shared_ptr<SymbolTable> popScope() {
        return parent_;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols_;

    std::shared_ptr<SymbolTable> parent_;
};