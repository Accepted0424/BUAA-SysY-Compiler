#pragma once
#include "lexer.h"

class Parser {
public:
    Parser(Lexer &lexer) : lexer_(lexer){};

private:
    Lexer &lexer_;
};

