#pragma once

#include <iostream>

namespace AstNode {
    struct ConstExp;
    struct AddExp;

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

    struct Ident : Node {
        std::string content;

        void print(std::ostream &out) override;

        Ident() = default;
        Ident(int lineno, const std::string &ident) : Node(lineno), value(ident) {}
    };

    struct CompUnit : Node {
        std::vector<std::unique_ptr<Decl>> var_decls;
        std::vector<std::unique_ptr<FuncDef>> func_defs;
        std::unique_ptr<MainFuncDef> main_func;

        void print(std::ostream &out) override;
    };

    struct Btype : Node {
        std::unique_ptr<std::string> type;

        void print(std::ostream &out) override;
    };

    struct ConstInitVal : Node {
        enum Kind {
            EXP,
            LIST
        } kind;
        std::unique_ptr<ConstExp> exp;
        std::vector<std::unique_ptr<ConstExp>> list;

        void print(std::ostream &out) override;
    };

    struct Exp : Node {
        std::unique_ptr<AddExp> addExp;

        void print(std::ostream &out) override;
    };

    struct LVal : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<Exp> index; // for array index

        void print(std::ostream &out) override;
    };

    struct Number : Node {
        std::string value;

        void print(std::ostream &out) override;
    };

    struct PrimaryExp : Node {
        enum Kind {
            EXP,
            LVAL,
            NUMBER
        } kind;
        std::unique_ptr<Exp> exp;       // '(' Exp ')'
        std::unique_ptr<LVal> lval;    // LVal
        std::unique_ptr<Number> number; // Number

        void print(std::ostream &out) override;
    };

    struct FuncRParams : Node {
        std::vector<std::unique_ptr<Exp>> params;

        void print(std::ostream &out) override;
    };

    struct UnaryOp : Node {
        enum Kind {
            PLUS,
            MINU,
            NOT
        } kind;

        void print(std::ostream &out) override;
    };

    struct UnaryExp : Node {
        enum Kind {
            PRIMARY,   // PrimaryExp
            CALL,      // Ident(FuncRParams?)
            UNARY_OP   // UnaryOp UnaryExp
        } kind;

        std::unique_ptr<PrimaryExp> primary;   // PRIMARY

        struct Call {
            std::unique_ptr<Ident> ident;
            std::unique_ptr<FuncRParams> params; // 可为空
        };
        std::unique_ptr<Call> call;            // CALL

        struct Unary {
            std::unique_ptr<UnaryOp> op;
            std::unique_ptr<UnaryExp> expr;
        };
        std::vector<Unary> unary;          // UNARY_OP

        void print(std::ostream &out) override;
    };

    struct MulExp : Node {
        enum Operator { MULT, DIV, MOD };

        std::unique_ptr<UnaryExp> first;
        std::vector<std::pair<Operator, std::unique_ptr<UnaryExp>>> rest;

        void print(std::ostream &out) override;
    };

    struct AddExp : Node {
        enum Operator { PLUS, MINU };

        std::unique_ptr<MulExp> first;
        std::vector<std::pair<Operator, std::unique_ptr<MulExp>>> rest;

        void print(std::ostream &out) override;
    };

    struct ConstExp : Node {
        std::unique_ptr<AddExp> addExp;

        void print(std::ostream &out) override;
    };

    struct ConstDef : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<ConstExp> constExp; // for array size
        std::unique_ptr<ConstInitVal> constInitVal;

        void print(std::ostream &out) override;
    };

    struct ConstDecl : Node {
        std::unique_ptr<Btype> btype;
        std::vector<std::unique_ptr<ConstDef>> constDefs;

        void print(std::ostream &out) override;
    };

    struct InitVal : Node {
        enum Kind {
            EXP,
            LIST
        } kind;

        std::unique_ptr<Exp> exp;
        std::vector<std::unique_ptr<Exp>> list;

        void print(std::ostream &out) override;
    };

    struct VarDef : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<ConstExp> constExp; // for array size, can be null
        std::unique_ptr<InitVal> initVal; // can be null

        void print(std::ostream &out) override;
    };

    struct VarDecl : Node {
        std::string prefix; // "static" or ""
        std::unique_ptr<Btype> btype;
        std::vector<std::unique_ptr<VarDef>> varDefs;

        void print(std::ostream &out) override;
    };

    struct FuncDef : Node {
        void print(std::ostream &out) override;
    };

    struct Stmt : Node {

        void print(std::ostream &out) override;
    };

    struct BlockItem : Node {
        enum Kind {
            DECL,
            STMT
        } kind;

        std::optional<std::unique_ptr<Decl>> decl;
        std::optional<std::unique_ptr<Stmt>> stmt;

        void print(std::ostream &out) override;
    };

    struct Block : Node {
        std::vector<std::unique_ptr<Node>> blockItems;

        void print(std::ostream &out) override;
    };

    struct MainFuncDef : FuncDef {
        std::unique_ptr<Block> block;

        void print(std::ostream &out) override;
    };

    struct stmts : Node {
        void print(std::ostream &out) override;
    };
}