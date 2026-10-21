// BGE Revived - logging and assertions.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace bge {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
};

void log_init(const char* app_name);
void log_shutdown();
void log_set_level(LogLevel level);
LogLevel log_level();
void log_message(LogLevel level, const char* file, int line, const char* fmt, ...);
void log_message_v(LogLevel level, const char* file, int line, const char* fmt, va_list args);

} // namespace bge

#define BGE_LOG(level, ...) ::bge::log_message(level, __FILE__, __LINE__, __VA_ARGS__)
#define BGE_TRACE(...) BGE_LOG(::bge::LogLevel::Trace, __VA_ARGS__)
#define BGE_DEBUG(...) BGE_LOG(::bge::LogLevel::Debug, __VA_ARGS__)
#define BGE_INFO(...)  BGE_LOG(::bge::LogLevel::Info, __VA_ARGS__)
#define BGE_WARN(...)  BGE_LOG(::bge::LogLevel::Warn, __VA_ARGS__)
#define BGE_ERROR(...) BGE_LOG(::bge::LogLevel::Error, __VA_ARGS__)

#define BGE_FATAL(...) do { \
    BGE_LOG(::bge::LogLevel::Fatal, __VA_ARGS__); \
    ::bge::log_shutdown(); \
    std::abort(); \
} while (0)

#if defined(_MSC_VER)
#define BGE_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define BGE_DEBUGBREAK() __builtin_trap()
#else
#define BGE_DEBUGBREAK() ((void)0)
#endif

#define BGE_ASSERT(cond, ...) do { \
    if (!(cond)) { \
        BGE_LOG(::bge::LogLevel::Fatal, __FILE__, __LINE__, "Assertion failed: %s", #cond); \
        if (sizeof(#__VA_ARGS__) > 1) BGE_LOG(::bge::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__); \
        BGE_DEBUGBREAK(); \
        std::abort(); \
    } \
} while (0)

#define BGE_VERIFY(cond) BGE_ASSERT(cond, "verification failed")
