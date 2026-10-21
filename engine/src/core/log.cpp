#include "bge/core/log.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace bge {
namespace {

std::mutex g_log_mutex;
std::atomic<int> g_log_level{static_cast<int>(LogLevel::Info)};
std::string g_app_name = "bge";
bool g_ansi = false;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

const char* level_color(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\x1b[90m";
        case LogLevel::Debug: return "\x1b[36m";
        case LogLevel::Info:  return "\x1b[37m";
        case LogLevel::Warn:  return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
        case LogLevel::Fatal: return "\x1b[91m";
    }
    return "";
}

const char* short_file(const char* file) {
    if (!file) return "?";
    const char* last = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

} // namespace

void log_init(const char* app_name) {
    if (app_name && *app_name) g_app_name = app_name;
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        g_ansi = true;
    }
#endif
}

void log_shutdown() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::fflush(stdout);
    std::fflush(stderr);
}

void log_set_level(LogLevel level) { g_log_level.store(static_cast<int>(level), std::memory_order_relaxed); }
LogLevel log_level() { return static_cast<LogLevel>(g_log_level.load(std::memory_order_relaxed)); }

void log_message(LogLevel level, const char* file, int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message_v(level, file, line, fmt, args);
    va_end(args);
}

void log_message_v(LogLevel level, const char* file, int line, const char* fmt, va_list args) {
    if (static_cast<int>(level) < g_log_level.load(std::memory_order_relaxed)) return;

    char message[4096];
    std::vsnprintf(message, sizeof(message), fmt, args);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_ansi) {
        std::fprintf(stdout, "\x1b[90m[%s]\x1b[0m %s%s\x1b[0m \x1b[90m%s:%d\x1b[0m %s\n",
                     stamp, level_color(level), level_name(level), short_file(file), line, message);
    } else {
        std::fprintf(stdout, "[%s] %s %s:%d %s\n", stamp, level_name(level), short_file(file), line, message);
    }
    std::fflush(stdout);
#if defined(_WIN32)
    if (level >= LogLevel::Warn) {
        char line_buf[4352];
        std::snprintf(line_buf, sizeof(line_buf), "[%s] %s %s:%d %s\n", stamp, level_name(level), short_file(file), line, message);
        OutputDebugStringA(line_buf);
    }
#endif
}

} // namespace bge
