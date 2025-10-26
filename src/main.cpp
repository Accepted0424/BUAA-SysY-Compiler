#include <fstream>
#include <iostream>

#include "error.h"
#include "lexer.h"
#include "logger.h"
#include "parser.h"
#include "visitor.h"
#include "llvm/include/ir/module.h"

// homework 2
void lex(std::ifstream& file, std::ofstream& out) {
    Lexer lexer(file);
    Token token;
    std::string info;

    lexer.next(token);
    while (token.type != Token::EOFTK) {
        // std::cout << token.type2string.at(token.type) << " " << token.content << " " << token.lineno << std::endl;
        std::cout << Token::toString(token) << " " << token.content << std::endl;
        out << Token::toString(token) << " " << token.content << std::endl;
        lexer.next(token);
    }
}

// homework 3
void parse(std::ifstream& file, std::ofstream& out) {
    Lexer lexer(file);
    Parser parser(lexer, out);

    parser.parse();
}

// homework 4
void semanticAnalyse(std::ifstream& file, std::ofstream& out) {
    Lexer lexer(file);
    Parser parser(lexer);
    auto root = parser.parse();

    ModulePtr module = Module::New("main");
    auto visitor = Visitor(module);
    visitor.visit(*root);
}

int main(int argc, char *argv[]) {
    std::ifstream infile("testfile.txt", std::ios::in);
    std::ofstream outfile("parser.txt", std::ios::out);
    std::ofstream errorfile("error.txt", std::ios::out);

    Logger::instance().setLevel(LogLevel::RELEASE);

    // lex(infile, outfile);
    // parse(infile, outfile);

    ErrorReporter::get().dump(errorfile);

    return 0;
}
