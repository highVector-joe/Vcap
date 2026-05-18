#pragma once
#include <string>
#include <mutex>
#include <cstdio>

namespace vcap {

enum class LogLevel { DEBUG = 0, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel lvl) { level_ = lvl; }
    void log(LogLevel lvl, const char* file, int line, const std::string& msg);

    // Convenience wrappers used by macros below
    static void debug(const char* f, int l, const std::string& m);
    static void info (const char* f, int l, const std::string& m);
    static void warn (const char* f, int l, const std::string& m);
    static void error(const char* f, int l, const std::string& m);

private:
    Logger() = default;
    LogLevel level_ = LogLevel::INFO;
    std::mutex mu_;
};

} // namespace vcap

// Convenient macros — stringify first so we never build the string when filtered
#define LOG_DEBUG(msg) vcap::Logger::debug(__FILE__, __LINE__, msg)
#define LOG_INFO(msg)  vcap::Logger::info (__FILE__, __LINE__, msg)
#define LOG_WARN(msg)  vcap::Logger::warn (__FILE__, __LINE__, msg)
#define LOG_ERROR(msg) vcap::Logger::error(__FILE__, __LINE__, msg)
