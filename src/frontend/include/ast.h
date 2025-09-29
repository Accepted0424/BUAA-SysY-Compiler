#pragma once

#include <iostream>

struct Node {
    int lineno;

    virtual void print(std::ostream &out) = 0;

    Node() = default;
    Node(int lineno) : lineno(lineno) {}
};

struct ConstDecl;
struct VarDecl;
using Decl = std::variant<ConstDecl, VarDecl>;

struct FuncDef;
struct MainFuncDef;

struct Ident : public Node {
    std::string value;

    void print(std::ostream &out) override;

    Ident() = default;
    Ident(int lineno, const std::string &ident) : Node(lineno), value(ident) {}
};

struct CompUnit : public Node {
    std::vector<std::unique_ptr<Decl>> var_decls;
    std::vector<std::unique_ptr<FuncDef>> func_defs;
    std::unique_ptr<MainFuncDef> main_func;

    void print(std::ostream &out) override;
};

struct ConstDecl : public Node {
    std::unique_ptr<Ident> ident;

    void print(std::ostream &out) override;
};

struct VarDecl : public Node {
    void print(std::ostream &out) override;
};

struct FuncDef : public Node {
    void print(std::ostream &out) override;
};

struct MainFuncDef : public FuncDef {

};

struct stmts : public Node {
    void print(std::ostream &out) override;
};