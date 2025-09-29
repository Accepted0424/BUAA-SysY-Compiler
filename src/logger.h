#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    RELEASE
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLogFile(const std::string& filename) {
        file_.open(filename, std::ios::app);
    }

    void setLevel(LogLevel level) {
        level_ = level;
    }

    template <typename... Args>
    void log(const LogLevel level, const int lineno, const std::string& msg, const Args&... args) {
        if (level < level_) return;

        const std::string level_str = levelToString(level);

        std::cout << "[" << level_str << "] " << lineno << ": " << msg << std::endl;
        ((std::cout << args), ...);  // 将 args 中的每个参数依次输出 ( C++17 )

        if (file_.is_open()) {
            file_ << "[" << level_str << "] " << lineno << ": " << msg << std::endl;
        }
    }

private:
    Logger() : level_(LogLevel::DEBUG) {}
    Logger(const Logger &) = delete; // Delete copy constructor to prevent copying of the singleton instance
    Logger &operator=(const Logger &) = delete; // Delete copy assignment operator to prevent assignment of the singleton instance
    ~Logger() { if (file_.is_open()) file_.close(); }

    static std::string levelToString(LogLevel level) {
        switch(level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::RELEASE: return "RELEASE";
        }
        return "UNKNOWN";
    }

    LogLevel level_;
    std::ofstream file_;
};

#define LOG_DEBUG(lineno, ...) Logger::instance().log(LogLevel::DEBUG, lineno, __VA_ARGS__)
#define LOG_INFO(lineno, ...)  Logger::instance().log(LogLevel::INFO, lineno, __VA_ARGS__)
#define LOG_WARN(lineno, ...)  Logger::instance().log(LogLevel::WARN, lineno, __VA_ARGS__)
#define LOG_ERROR(lineno, ...) Logger::instance().log(LogLevel::ERROR, lineno, __VA_ARGS__)
