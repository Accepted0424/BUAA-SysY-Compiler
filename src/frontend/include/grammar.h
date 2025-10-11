#pragma once

#include <vector>
#include <string>
#include <iostream>

#include "ast.h"

#define LETTERS \
{"a"},{"b"},{"c"},{"d"},{"e"},{"f"},{"g"},{"h"},{"i"},{"j"},{"k"},{"l"},{"m"}, \
{"n"},{"o"},{"p"},{"q"},{"r"},{"s"},{"t"},{"u"},{"v"},{"w"},{"x"},{"y"},{"z"}, \
{"A"},{"B"},{"C"},{"D"},{"E"},{"F"},{"G"},{"H"},{"I"},{"J"},{"K"},{"L"},{"M"}, \
{"N"},{"O"},{"P"},{"Q"},{"R"},{"S"},{"T"},{"U"},{"V"},{"W"},{"X"},{"Y"},{"Z"}

#define DIGITS {"0"}, {"1"}, {"2"}, {"3"}, {"4"}, {"5"}, {"6"}, {"7"}, {"8"}, {"9"}

#define ADD_LETTERS_RULE addRule("letter", { \
{"a"},{"b"},{"c"},{"d"},{"e"},{"f"},{"g"},{"h"},{"i"},{"j"},{"k"},{"l"},{"m"}, \
{"n"},{"o"},{"p"},{"q"},{"r"},{"s"},{"t"},{"u"},{"v"},{"w"},{"x"},{"y"},{"z"}, \
{"A"},{"B"},{"C"},{"D"},{"E"},{"F"},{"G"},{"H"},{"I"},{"J"},{"K"},{"L"},{"M"}, \
{"N"},{"O"},{"P"},{"Q"},{"R"},{"S"},{"T"},{"U"},{"V"},{"W"},{"X"},{"Y"},{"Z"} \
});
#define ADD_DIGITS_RULE addRule("digit", {{"0"}, {"1"}, {"2"}, {"3"}, {"4"}, {"5"}, {"6"}, {"7"}, {"8"}, {"9"}});

#define SQUARE_BRACKETS(sym) #sym "_opt"
#define SQUARE_BRACKETS_TO_BNF(sym) addRule(#sym "_opt", {{#sym}, {}); // optional
#define CURLY_BRACKETS(sym) #sym "_rep"
#define CURLY_BRACKETS_TO_BNF(sym) addRule(#sym "_rep", {{#sym, #sym "_rep"}, {}}); // repeat

// 产生式规则
struct Rule {
    std::string lhs; // 左部非终结符
    std::vector<std::vector<std::string>> rhs; // 右部符号序列（可以包含终结符和非终结符）

    Rule() = default;
    Rule(const std::string& left, const std::vector<std::vector<std::string>>& right)
        : lhs(left), rhs(right) {}
};

// 文法
struct Grammar {
    std::vector<std::string> nonTerminals;   // 非终结符集合
    std::vector<std::string> terminals;      // 终结符集合
    std::vector<Rule> rules;                 // 产生式集合
    std::string startSymbol;                 // 起始符号

    // 添加规则
    void addRule(const std::string& lhs, const std::vector<std::vector<std::string>>& rhs) {
        rules.emplace_back(lhs, rhs);
    }

    // 检查是否是终结符
    bool isTerminal(const std::string& symbol) const {
        for (const auto &t : terminals)
            if (t == symbol) return true;
        return false;
    }

    // 检查是否是非终结符
    bool isNonTerminal(const std::string& symbol) const {
        for (const auto &nt : nonTerminals)
            if (nt == symbol) return true;
        return false;
    }
};

struct CompUnit : Grammar {
    CompUnit() {

    }
};
