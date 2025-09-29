#pragma once

#include <vector>
#include <string>
#include <iostream>

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

struct Ident : Grammar {
    Ident() {
        nonTerminals = {"identifier-nondigit", "digit"};
        terminals = { LETTERS, DIGITS, "_"};
        startSymbol = "identifier";

        addRule("identifier", {{"identifier-nondigit"}, {"identifier", "identifier-nondigit"}, {"identifier", "digit"}});
        ADD_LETTERS_RULE;
        ADD_DIGITS_RULE;
    }
};

struct IntConst : Grammar {
    IntConst() {
        nonTerminals = {"decimal-const", "nonzero-digit"};
        terminals = { DIGITS, "_"};
        startSymbol = "integer-const";

        addRule("integer-const", {{"decimal-const"}, {"0"}});
        addRule("decimal-const", {{"nonzero-digit"}, {"decimal-const", "digit"}});
        addRule("nonzero-digits", {{"1"}, {"2"}, {"3"}, {"4"}, {"5"}, {"6"}, {"7"}, {"8"}, {"9"}});
        ADD_DIGITS_RULE;
    }
};

struct StringConst : Grammar {
    StringConst() {
        nonTerminals = {"StringConst", "Char", "FormatChar", "NormalChar", "Char_rep"};
        terminals = { LETTERS, DIGITS, " ", "!", "(", ")", }; // Todo
        startSymbol = "String-const";

        addRule("StringConst", {{"\"", CURLY_BRACKETS("Char"), "\""}});
        CURLY_BRACKETS_TO_BNF("Char");
        addRule("Char", {{"FormatChar"}, {"NormalChar"}});
        addRule("NormalChar", {
            DIGITS, LETTERS,
            {" "}, {"!"}, // 32, 33
            {"("}, {")"}, {"*"}, { "+"}, {","}, {"-"}, {"."}, {"/"}, // 40–47
            {":"}, {";"}, {"<"}, {"="}, {">"}, {"?"}, {"@"}, // 58–64
            {"["}, {"]"}, {"^"}, {"_"}, {"`"}, // 91–96 （注意 92 '\' 已特殊处理）
            {"{"}, {"|"}, {"}"}, {"~"}, // 123–126
            {"\\n"} // 特殊：92 '\' + 'n'
        });
        addRule("FormatChar", {{"%d"}});
    }
};
