#pragma once

#include <iostream>

namespace AstNode {
    struct Block;
    struct ConstExp;
    struct AddExp;

    struct Node {
        int lineno;

        // virtual void print(std::ostream &out) = 0;

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

        // void print(std::ostream &out) override;

        Ident() = default;
        Ident(int lineno, const std::string &ident) : Node(lineno), content(ident) {}
    };

    struct CompUnit : Node {
        std::vector<std::unique_ptr<Decl>> var_decls;
        std::vector<std::unique_ptr<FuncDef>> func_defs;
        std::unique_ptr<MainFuncDef> main_func;

        // void print(std::ostream &out) override;
    };

    struct Btype : Node {
        std::unique_ptr<std::string> type;

        // void print(std::ostream &out) override;
    };

    struct ConstInitVal : Node {
        enum Kind {
            EXP,
            LIST
        } kind;
        std::unique_ptr<ConstExp> exp;
        std::vector<std::unique_ptr<ConstExp>> list;

        // void print(std::ostream &out) override;
    };

    struct Exp : Node {
        std::unique_ptr<AddExp> addExp;

        // void print(std::ostream &out) override;
    };

    struct LVal : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<Exp> index; // for array index

        // void print(std::ostream &out) override;
    };

    struct Number : Node {
        std::string value;

        // void print(std::ostream &out) override;
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

        // void print(std::ostream &out) override;
    };

    struct FuncRParams : Node {
        std::vector<std::unique_ptr<Exp>> params;

        // void print(std::ostream &out) override;
    };

    struct UnaryOp : Node {
        enum Kind {
            PLUS,
            MINU,
            NOT
        } kind;

        // void print(std::ostream &out) override;
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

        // void print(std::ostream &out) override;
    };

    struct MulExp : Node {
        enum Operator { MULT, DIV, MOD };

        std::unique_ptr<UnaryExp> first;
        std::vector<std::pair<Operator, std::unique_ptr<UnaryExp>>> rest;

        // void print(std::ostream &out) override;
    };

    struct AddExp : Node {
        enum Operator { PLUS, MINU };

        std::unique_ptr<MulExp> first;
        std::vector<std::pair<Operator, std::unique_ptr<MulExp>>> rest;

        // void print(std::ostream &out) override;
    };

    struct ConstExp : Node {
        std::unique_ptr<AddExp> addExp;

        // void print(std::ostream &out) override;
    };

    struct ConstDef : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<ConstExp> constExp; // for array size
        std::unique_ptr<ConstInitVal> constInitVal;

        // void print(std::ostream &out) override;
    };

    struct ConstDecl : Node {
        std::unique_ptr<Btype> btype;
        std::vector<std::unique_ptr<ConstDef>> constDefs;

        // void print(std::ostream &out) override;
    };

    struct InitVal : Node {
        enum Kind {
            EXP,
            LIST
        } kind;

        std::unique_ptr<Exp> exp;
        std::vector<std::unique_ptr<Exp>> list;

        // void print(std::ostream &out) override;
    };

    struct VarDef : Node {
        std::unique_ptr<Ident> ident;
        std::unique_ptr<ConstExp> constExp; // for array size, can be null
        std::unique_ptr<InitVal> initVal; // can be null

        // void print(std::ostream &out) override;
    };

    struct VarDecl : Node {
        std::string prefix; // "static" or ""
        std::unique_ptr<Btype> btype;
        std::vector<std::unique_ptr<VarDef>> varDefs;

        // void print(std::ostream &out) override;
    };

    struct FuncType : Node {
        enum Kind {
            VOID,
            INT
        } kind;

        // void print(std::ostream &out) override;
    };

    struct FuncFParam : Node {
        std::unique_ptr<Btype> btype;
        std::unique_ptr<Ident> ident;
        std::unique_ptr<ConstExp> constExp; // for array size, can be null

        // void print(std::ostream &out) override;
    };

    struct FuncFParams : Node {
        std::vector<std::unique_ptr<FuncFParam>> params;

        // void print(std::ostream &out) override;
    };

    struct FuncDef : Node {
        std::unique_ptr<FuncType> funcType;
        std::unique_ptr<Ident> ident;
        std::unique_ptr<FuncFParams> funcFParams; // can be null
        std::unique_ptr<Block> block;

        // void print(std::ostream &out) override;
    };

    struct ForStmt : Node {
        std::vector<std::pair<std::unique_ptr<LVal>, std::unique_ptr<Exp>>> assigns;

        // void print(std::ostream &out) override;
    };

    struct RelExp : Node {
        std::unique_ptr<AddExp> addExpFirst;
        enum Operator { LSS, GRE, LEQ, GEQ };
        std::vector<std::pair<Operator, std::unique_ptr<AddExp>>> addExpRest;

        // void print(std::ostream &out) override;
    };

    struct EqExp : Node {
        std::unique_ptr<RelExp> relExpFirst;
        enum Operator { EQL, NEQ };
        std::vector<std::pair<Operator, std::unique_ptr<RelExp>>> relExpRest;

        // void print(std::ostream &out) override;
    };

    struct LAndExp : Node {
        std::vector<std::unique_ptr<EqExp>> eqExps;

        // void print(std::ostream &out) override;
    };

    struct LOrExp : Node {
        std::vector<std::unique_ptr<LAndExp>> eqExps;

        // void print(std::ostream &out) override;
    };

    struct Cond : Node {
        std::unique_ptr<LOrExp> lOrExp;
    };

    struct Stmt : Node {
        enum kind {
            ASSIGN,     // LVal '=' Exp ';'
            EXP,        // [Exp] ';'
            BLOCK,      // Block
            IF,         // 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
            FOR,        // 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
            BREAK,      // 'break' ';'
            CONTINUE,   // 'continue' ';'
            RETURN,     // 'return' [Exp] ';'
            PRINTF      // 'printf' '(' StringConst {',' Exp} ')' ';'
        } kind;

        struct Assign {
            std::unique_ptr<LVal> lVal;
            std::unique_ptr<Exp> exp;
        } assignStmt;

        std::unique_ptr<Exp> exp;

        std::unique_ptr<Block> block;

        struct If {
            std::unique_ptr<Cond> cond;
            std::unique_ptr<Stmt> thenStmt;
            std::unique_ptr<Stmt> elseStmt; // can be null
        } ifStmt;

        struct For {
            std::unique_ptr<ForStmt> forStmtFirst;
            std::unique_ptr<Cond> cond;
            std::unique_ptr<ForStmt> forStmtSecond;
            std::unique_ptr<Stmt> stmt;
        } forStmt;

        std::unique_ptr<Exp> returnExp;

        struct Printf {
            std::string str;
            std::vector<std::unique_ptr<Exp>> args;
        } printfStmt;

        std::unique_ptr<Printf> printf;

        // void print(std::ostream &out) override;
    };

    struct BlockItem : Node {
        enum Kind {
            DECL,
            STMT
        } kind;

        std::optional<std::unique_ptr<Decl>> decl;
        std::optional<std::unique_ptr<Stmt>> stmt;

        // void print(std::ostream &out) override;
    };

    struct Block : Node {
        std::vector<std::unique_ptr<Node>> blockItems;

        // void print(std::ostream &out) override;
    };

    struct MainFuncDef : FuncDef {
        std::unique_ptr<Block> block;

        // void print(std::ostream &out) override;
    };

    struct stmts : Node {
        // void print(std::ostream &out) override;
    };
}