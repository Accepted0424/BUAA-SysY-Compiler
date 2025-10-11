#pragma once

#include <any>

#include "ast.h"
#include "lexer.h"

using namespace AstNode;

class Parser {
public:
    explicit Parser(Lexer &lexer) : lexer_(lexer) {
        lexer_.next(lookahead_);
    };

private:
    Lexer &lexer_;

    Token last_, token_, lookahead_;

    int braces_ = 0;

    void getToken();

    void ungetToken();

    bool match(Token::TokenType expected);

    bool is(Token::TokenType type);

    bool is(Token::TokenType type, Token::TokenType ahead_type);

    void parse();

    std::unique_ptr<Ident> parseIdent();

    std::unique_ptr<Decl> parseDecl();

    std::unique_ptr<Btype> parseBType();

    std::unique_ptr<ConstDecl> parseConstDecl();

    std::unique_ptr<ConstDef> parseConstDef();

    std::unique_ptr<Exp> parseExp();

    std::unique_ptr<LVal> parseLVal();

    std::unique_ptr<Number> parseNumber();

    std::unique_ptr<PrimaryExp> parsePrimaryExp();

    std::unique_ptr<UnaryOp> parseUnaryOp();

    std::unique_ptr<FuncRParams> parseFuncRParams();

    std::unique_ptr<UnaryExp> parseUnaryExp();

    std::unique_ptr<MulExp> parseMulExp();

    std::unique_ptr<AddExp> parseAddExp();

    std::unique_ptr<ConstExp> parseConstExp();

    std::unique_ptr<ConstInitVal> parseConstInitVal();

    std::unique_ptr<InitVal> parseInitVal();

    std::unique_ptr<VarDef> parseVarDef();

    std::unique_ptr<VarDecl> parseVarDecl();

    std::unique_ptr<FuncDef> parseFuncDef();

    bool parseStmt();

    bool parseBlockItem();

    std::unique_ptr<Block> parseBlock();

    std::unique_ptr<MainFuncDef> parseMainFuncDef();

    std::unique_ptr<CompUnit> parseCompUnit();
};

