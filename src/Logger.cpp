#include "Logger.h"
#include <ctime>
#include <cstdio>
#include <cstring>

namespace vcap {

namespace {
const char* levelStr(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

const char* levelColor(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "\033[36m";  // cyan
        case LogLevel::INFO:  return "\033[32m";  // green
        case LogLevel::WARN:  return "\033[33m";  // yellow
        case LogLevel::ERROR: return "\033[31m";  // red
    }
    return "";
}

const char* RESET = "\033[0m";
} // anon

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::log(LogLevel lvl, const char* file, int line, const std::string& msg) {
    if (lvl < level_) return;

    // Timestamp
    time_t t = time(nullptr);
    struct tm* tm_info = localtime(&t);
    char tsbuf[32];
    strftime(tsbuf, sizeof(tsbuf), "%H:%M:%S", tm_info);

    // Strip path from filename
    const char* fname = strrchr(file, '/');
    fname = fname ? fname + 1 : file;

    std::lock_guard<std::mutex> lock(mu_);
    fprintf(stderr, "%s%s%s [%s] %s:%d  %s\n",
            levelColor(lvl), levelStr(lvl), RESET,
            tsbuf, fname, line, msg.c_str());
    fflush(stderr);
}

void Logger::debug(const char* f, int l, const std::string& m) { instance().log(LogLevel::DEBUG, f, l, m); }
void Logger::info (const char* f, int l, const std::string& m) { instance().log(LogLevel::INFO,  f, l, m); }
void Logger::warn (const char* f, int l, const std::string& m) { instance().log(LogLevel::WARN,  f, l, m); }
void Logger::error(const char* f, int l, const std::string& m) { instance().log(LogLevel::ERROR, f, l, m); }

} // namespace vcap
