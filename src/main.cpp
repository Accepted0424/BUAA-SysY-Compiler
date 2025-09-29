#include <fstream>
#include <iostream>

#include "error.h"
#include "lexer.h"
#include "logger.h"

void lex(std::ifstream& file, std::ofstream& out) {
    Lexer lexer(file);
    Token token;
    std::string info;

    lexer.next(token);
    while (token.type != Token::EOFTK) {
        // std::cout << token.type2string.at(token.type) << " " << token.content << " " << token.lineno << std::endl;
        std::cout << token.type2string.at(token.type) << " " << token.content << std::endl;
        out << token.type2string.at(token.type) << " " << token.content << std::endl;
        lexer.next(token);
    }
}

int main(int argc, char *argv[]) {
    std::ifstream infile("testfile.txt", std::ios::in);
    std::ofstream outfile("lexer.txt", std::ios::out);
    std::ofstream errorfile("error.txt", std::ios::out);

    Logger::instance().setLevel(LogLevel::RELEASE);

    lex(infile, outfile);

    ErrorReporter::get().dump(errorfile);

    return 0;
}
