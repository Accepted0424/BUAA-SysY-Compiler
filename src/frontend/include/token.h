#pragma once

#include <string>
#include <unordered_map>
#include <utility>

// X macro magic
#define TOKEN_TYPE                                                              \
    X(IDENFR, "Ident")                                                          \
    X(INTCON, "IntConst")                                                       \
    X(STRCON, "StringConst")                                                    \
    X(CONSTTK, "const")                                                         \
    X(INTTK, "int")                                                             \
    X(STATICTK, "static")                                                       \
    X(BREAKTK, "break")                                                         \
    X(CONTINUETK, "continue")                                                   \
    X(IFTK, "if")                                                               \
    X(MAINTK, "main")                                                           \
    X(ELSETK, "else")                                                           \
    X(NOT, "!")                                                                 \
    X(AND, "&&")                                                                \
    X(OR, "||")                                                                 \
    X(FORTK, "for")                                                             \
    X(RETURNTK, "return")                                                       \
    X(VOIDTK, "void")                                                           \
    X(PLUS, "+")                                                                \
    X(MINU, "-")                                                                \
    X(PRINTFTK, "printf")                                                       \
    X(MULT, "*")                                                                \
    X(DIV, "/")                                                                 \
    X(MOD, "%")                                                                 \
    X(LSS, "<")                                                                 \
    X(LEQ, "<=")                                                                \
    X(GRE, ">")                                                                 \
    X(GEQ, ">=")                                                                \
    X(EQL, "==")                                                                \
    X(NEQ, "!=")                                                                \
    X(ASSIGN, "=")                                                              \
    X(SEMICN, ";")                                                              \
    X(COMMA, ",")                                                               \
    X(LPARENT, "(")                                                             \
    X(RPARENT, ")")                                                             \
    X(LBRACK, "[")                                                              \
    X(RBRACK, "]")                                                              \
    X(LBRACE, "{")                                                              \
    X(RBRACE, "}")                                                              \
    X(EOFTK, "EOF")

struct Token {
    enum TokenType {
#define X(a, b) a,
        TOKEN_TYPE
#undef X
    } type;

    static const std::unordered_map<TokenType, std::string>& getTypeMap() {
        static const std::unordered_map<TokenType, std::string> type2String{
#define X(a, b) {TokenType::a, b},
            TOKEN_TYPE
#undef X
        };
        return type2String;
    }

    static std::string toString(TokenType t) {
        const auto& map = getTypeMap();
        if (auto it = map.find(t); it != map.end())
            return it->second;
        return "UNKNOWN";
    }

    static std::string toString(const Token& t) {
        return toString(t.type);
    }

    std::string content;
    int lineno;

    Token() = default;

    Token(const TokenType type, std::string content, const int lineno)
        : type(type), content(std::move(content)), lineno(lineno) {}
};