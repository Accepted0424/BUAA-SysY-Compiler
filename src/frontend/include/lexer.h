#pragma once

#include "token.h"
#include <istream>

class Lexer {
public:
    Lexer(std::istream &in) : input_(in) {}

    void next(Token &token);

private:
    static const std::unordered_map<std::string, Token::TokenType> keywords_;

    std::istream &input_;

    int lineno_ = 1;

    char peekChar();

    char getChar();

    void ungetChar();

    bool lexIdentifier(Token &token, std::string content);

    bool lexIntConst(Token &token, std::string content);

    bool lexStringConst(Token &token, std::string content);

    bool lexAndExpr(Token &token, std::string content);

    bool lexOrExpr(Token &token, std::string content);

    bool lexNeq(Token &token, std::string content);

    bool lexEql(Token &token, std::string content);

    bool lexLeq(Token &token, std::string content);

    bool lexGeq(Token &token, std::string content);

    bool lexSingleLineComment();

    bool lexBlockComment();
};
