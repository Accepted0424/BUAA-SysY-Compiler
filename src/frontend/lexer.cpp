#include "lexer.h"

#include "error.h"
#include "logger.h"

const std::unordered_map<std::string, Token::TokenType> Lexer::keywords_ = {
    {"const", Token::CONSTTK}, {"int", Token::INTTK}, {"static", Token::STATICTK},
    {"break", Token::BREAKTK}, {"continue", Token::CONTINUETK}, {"if", Token::IFTK},
    {"main", Token::MAINTK}, {"else", Token::ELSETK}, {"for", Token::FORTK},
    {"return", Token::RETURNTK}, {"void", Token::VOIDTK}, {"printf", Token::PRINTFTK}
};

char Lexer::peekChar() {
    const std::istream::int_type ch = input_.peek();
    if (ch == std::char_traits<char>::eof()) {
        // EOF(-1)
    }
    return static_cast<unsigned char>(ch);
}

char Lexer::getChar() {
    const std::istream::int_type ch = input_.get();
    if (ch == std::char_traits<char>::eof()) {
        // EOF(-1)
    }
    return static_cast<unsigned char>(ch);
}

void Lexer::ungetChar() {
    input_.unget();
}

bool Lexer::lexIdentifier(Token &token, std::string content) {
    char ch = getChar();
    while (isalnum(ch) || ch == '_') {
        content.append(1, ch);
        ch = getChar();
    }
    ungetChar();

    auto it = keywords_.find(content);
    if ( it != keywords_.end()) {
        token = Token(it->second, content, lineno_);
        return true;
    }

    token = Token(Token::IDENFR, content, lineno_);
    return true;
}

bool Lexer::lexIntConst(Token &token, std::string content) {
    char ch = getChar();
    while (isdigit(ch)) {
        content.append(1, ch);
        ch = getChar();
    }
    ungetChar();

    token = Token(Token::INTCON, content, lineno_);
    return true;
}

bool Lexer::lexStringConst(Token &token, std::string content) {
     char ch = getChar();
     while (ch == 32 || ch == 33 || (ch >= 40 && ch <= 126) || ch == '%') {
         content.append(1, ch);
         ch = getChar();
     }

    if (ch == '"') {
        content.append(1, ch);
        token = Token(Token::STRCON, content, lineno_);
        return true;
    }

    ungetChar();
    LOG_ERROR(lineno_, "[Lexer] Unterminated StringConst");
    return false;
}

bool Lexer::lexAndExpr(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '&') {
        content.append(1, ch);
        token = Token(Token::AND, content, lineno_);
        return true;
    }
    ungetChar();
    ErrorReporter::error(lineno_, ERR_ILLEGAL_SYMBOL);
    LOG_ERROR(lineno_, "[Lexer] Unterminated AndExpr");
    return false;
}

bool Lexer::lexOrExpr(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '|') {
        content.append(1, ch);
        token = Token(Token::OR, content, lineno_);
        return true;
    }
    ungetChar();
    LOG_ERROR(lineno_, "[Lexer] Unterminated OrExpr");
    return false;
}

bool Lexer::lexNeq(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '=') {
        content.append(1, ch);
        token = Token(Token::NEQ, content, lineno_);
        return true;
    }
    ungetChar();
    return false;
}

bool Lexer::lexEql(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '=') {
        content.append(1, ch);
        token = Token(Token::EQL, content, lineno_);
        return true;
    }
    ungetChar();
    return false;
}

bool Lexer::lexLeq(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '=') {
        content.append(1, ch);
        token = Token(Token::LEQ, content, lineno_);
        return true;
    }
    ungetChar();
    return false;
}

bool Lexer::lexGeq(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '=') {
        content.append(1, ch);
        token = Token(Token::GEQ, content, lineno_);
        return true;
    }
    ungetChar();
    return false;
}

bool Lexer::lexSingleLineComment(Token &token, std::string content) {
    char ch = getChar();
    if (ch == '/') {
        while (ch != '\n') {
            ch = getChar();
        }
        return true;
    }
    return false;
}

void Lexer::next(Token &token) {
    std::string content;

    char ch = getChar();
    content.append(1, ch);

    if (isalpha(ch) || ch == '_') {
        if (!lexIdentifier(token, content)) return next(token);
    } else if (isdigit(ch)) {
        if (!lexIntConst(token, content)) return next(token);
    } else if (ch == '\"') {
        if (!lexStringConst(token, content)) return next(token);
    } else if (ch == '&') {
        if (!lexAndExpr(token, content)) return next(token);
    } else if (ch == '|') {
        if (!lexOrExpr(token, content)) return next(token);
    } else if (ch == '!') {
        if (!lexNeq(token, content))
            token = Token(Token::NOT, content, lineno_);
    } else if (ch == '<') {
        if (!lexLeq(token, content))
            token = Token(Token::LSS, content, lineno_);
    } else if (ch == '>') {
        if (!lexGeq(token, content))
            token = Token(Token::GRE, content, lineno_);
    } else if (ch == '=') {
        if (!lexEql(token, content))
            token = Token(Token::ASSIGN, content, lineno_);
    } else if (ch == '\n') {
        lineno_++;
        return next(token);
    } else if (ch == '/') {
        if (lexSingleLineComment(token, content)) return next(token);
        token = Token(Token::DIV, content, lineno_);
    } else if (ch == '(') {
        token = Token(Token::LPARENT, content, lineno_);
    } else if (ch == ')') {
        token = Token(Token::RPARENT, content, lineno_);
    } else if (ch == '[') {
        token = Token(Token::LBRACK, content, lineno_);
    } else if (ch == ']') {
        token = Token(Token::RBRACK, content, lineno_);
    } else if (ch == '{') {
        token = Token(Token::LBRACE, content, lineno_);
    } else if (ch == '}') {
        token = Token(Token::RBRACE, content, lineno_);
    } else if (ch == ';') {
        token = Token(Token::SEMICN, content, lineno_);
    } else if (ch == '+') {
        token = Token(Token::PLUS, content, lineno_);
    } else if (ch == '-') {
        token = Token(Token::MINU, content, lineno_);
    } else if (ch == '*') {
        token = Token(Token::MULT, content, lineno_);
    } else if (ch == ',') {
        token = Token(Token::COMMA, content, lineno_);
    } else if (ch == '%') {
        token = Token(Token::MOD, content, lineno_);
    } else if (isblank(ch)) {
        return next(token);
    } else if (ch == EOF) {
        token = Token(Token::EOFTK, content, lineno_);
    } else {
        LOG_ERROR(lineno_, "invalid character: " + content);
    }
}
