#pragma once

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

// Error codes for semantic and syntax analysis
#define ERR_ILLEGAL_SYMBOL               "a" // 非法符号
#define ERR_REDEFINED_NAME               "b" // 名字重定义
#define ERR_UNDEFINED_NAME               "c" // 未定义的名字
#define ERR_FUNC_ARG_COUNT_MISMATCH      "d" // 函数参数个数不匹配
#define ERR_FUNC_ARG_TYPE_MISMATCH       "e" // 函数参数类型不匹配
#define ERR_VOID_FUNC_RETURN_MISMATCH    "f" // 无返回值的函数存在不匹配的 return 语句
#define ERR_NONVOID_FUNC_MISSING_RETURN  "g" // 有返回值的函数缺少 return 语句
#define ERR_CONST_ASSIGNMENT             "h" // 不能改变常量的值
#define ERR_MISSING_SEMICOLON            "i" // 缺少分号
#define ERR_MISSING_RPARENT              "j" // 缺少右小括号 ')'
#define ERR_MISSING_RBRACK               "k" // 缺少右中括号 ']'
#define ERR_PRINTF_ARG_MISMATCH          "l" // printf 中格式字符与表达式个数不匹配
#define ERR_BREAK_CONTINUE_OUTSIDE_LOOP  "m" // 在非循环块中使用 break 和 continue 语句

struct Error {
    int lineno;
    std::string msg;
};

class ErrorReporter {
public:
    static ErrorReporter &get() {
        static ErrorReporter instance;
        return instance;
    }

    static void error(int lineno, const std::string msg) {
        get().report_error(lineno, msg);
    }

    void report_error(int lineno, const std::string msg) {
        errors_.push_back({lineno, msg});
    }

    bool has_error() const { return !errors_.empty(); }

    void dump(std::ostream &out) {
        std::sort(
            errors_.begin(), errors_.end(),
            [](const Error &a, const Error &b) { return a.lineno < b.lineno; });

        for (const auto &err : errors_) {
            out << err.lineno << " " << err.msg << std::endl;
        }
    }

private:
    ErrorReporter() = default;
    ErrorReporter(const ErrorReporter &) = delete;
    ErrorReporter &operator=(const ErrorReporter &) = delete;

    std::vector<Error> errors_;
};
