#pragma once

#include <any>
#include <fstream>

#include "ast.h"
#include "lexer.h"

using namespace AstNode;

class Parser {
public:
    explicit Parser(Lexer &lexer) : lexer_(lexer), out_(std::nullopt) {
        lexer_.next(lookahead_);
    }

    explicit Parser(Lexer &lexer, std::ofstream &out) : lexer_(lexer), out_(out) {
        lexer_.next(lookahead_);
    }

    std::unique_ptr<CompUnit> parse();

private:
    Lexer &lexer_;

    bool has_unget_ = false;

    Token last_, cur_, lookahead_, last_lookahead_;

    std::optional<std::reference_wrapper<std::ofstream>> out_;

    void printNode(std::string node);

    void getToken();

    void ungetToken();

    void skipUntilSemicn();

    bool match(Token::TokenType expected);

    bool is(Token::TokenType type);

    bool is(Token::TokenType type, Token::TokenType ahead_type);

    std::unique_ptr<Ident> parseIdent();

    std::unique_ptr<Decl> parseDecl();

    std::unique_ptr<Btype> parseBType();

    std::unique_ptr<ConstDecl> parseConstDecl();

    std::unique_ptr<ConstDef> parseConstDef();

    std::unique_ptr<Exp> parseExp();

    std::unique_ptr<Exp> parseExp(std::unique_ptr<LVal> lval);

    std::unique_ptr<LVal> parseLVal();

    std::unique_ptr<LVal> parseLValSilent();

    std::unique_ptr<Number> parseNumber();

    std::unique_ptr<PrimaryExp> parsePrimaryExp();

    std::unique_ptr<PrimaryExp> parsePrimaryExp(std::unique_ptr<LVal> lval);

    std::unique_ptr<UnaryOp> parseUnaryOp();

    std::unique_ptr<FuncRParams> parseFuncRParams();

    std::unique_ptr<UnaryExp> parseUnaryExp();

    std::unique_ptr<UnaryExp> parseUnaryExp(std::unique_ptr<LVal> lval);

    std::unique_ptr<MulExp> parseMulExp();

    std::unique_ptr<MulExp> parseMulExp(std::unique_ptr<LVal> lval);

    std::unique_ptr<AddExp> parseAddExp();

    std::unique_ptr<AddExp> parseAddExp(std::unique_ptr<LVal> lval);

    std::unique_ptr<ConstExp> parseConstExp();

    std::unique_ptr<ConstInitVal> parseConstInitVal();

    std::unique_ptr<InitVal> parseInitVal();

    std::unique_ptr<VarDef> parseVarDef();

    std::unique_ptr<VarDecl> parseVarDecl();

    std::unique_ptr<ForStmt> parseForStmt();

    std::unique_ptr<RelExp> parseRelExp();

    std::unique_ptr<EqExp> parseEqExp();

    std::unique_ptr<LAndExp> parseLAndExp();

    std::unique_ptr<LOrExp> parseLOrExp();

    std::unique_ptr<Cond> parseCond();

    std::unique_ptr<FuncDef> parseFuncDef();

    std::unique_ptr<Stmt> parseStmt();

    std::unique_ptr<BlockItem> parseBlockItem();

    std::unique_ptr<Block> parseBlock();

    std::unique_ptr<MainFuncDef> parseMainFuncDef();

    std::unique_ptr<FuncType> parseFuncType();

    std::unique_ptr<FuncFParam> parseFuncFParam();

    std::unique_ptr<FuncFParams> parseFuncFParams();

    std::unique_ptr<CompUnit> parseCompUnit();
};

