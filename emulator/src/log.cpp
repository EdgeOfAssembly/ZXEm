/**
 * @file log.cpp
 * @brief Reverse-engineering logger implementation.
 */

#include "log.h"

#include <strings.h>
#include <ctime>

Log& Log::instance()
{
    static Log g;
    return g;
}

Log::Log() = default;

Log::~Log()
{
    if (file_ != nullptr)
    {
        fclose(file_);
        file_ = nullptr;
    }
}

void Log::set_enabled(bool on)
{
    enabled_ = on;
}

bool Log::enabled() const
{
    return enabled_;
}

void Log::set_level(LogLevel level)
{
    level_ = level;
}

LogLevel Log::level() const
{
    return level_;
}

bool Log::set_file(const char* path)
{
    if (file_ != nullptr)
    {
        fclose(file_);
        file_ = nullptr;
        file_path_.clear();
    }
    if (path == nullptr || path[0] == '\0')
    {
        return true;
    }
    FILE* f = fopen(path, "w");
    if (f == nullptr)
    {
        return false;
    }
    file_ = f;
    file_path_ = path;
    return true;
}

void Log::set_trace_cpu(bool on)
{
    trace_cpu_ = on;
}

void Log::set_trace_io(bool on)
{
    trace_io_ = on;
}

bool Log::trace_cpu() const
{
    return trace_cpu_;
}

bool Log::trace_io() const
{
    return trace_io_;
}

LogLevel Log::parse_level(const char* s, bool* ok)
{
    if (ok != nullptr)
    {
        *ok = true;
    }
    if (s == nullptr)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        return LogLevel::info;
    }
    if (strcasecmp(s, "error") == 0 || strcasecmp(s, "err") == 0)
    {
        return LogLevel::error;
    }
    if (strcasecmp(s, "warn") == 0 || strcasecmp(s, "warning") == 0)
    {
        return LogLevel::warn;
    }
    if (strcasecmp(s, "info") == 0)
    {
        return LogLevel::info;
    }
    if (strcasecmp(s, "debug") == 0)
    {
        return LogLevel::debug;
    }
    if (strcasecmp(s, "trace") == 0)
    {
        return LogLevel::trace;
    }
    if (ok != nullptr)
    {
        *ok = false;
    }
    return LogLevel::info;
}

const char* Log::level_name(LogLevel level)
{
    switch (level)
    {
        case LogLevel::error: return "error";
        case LogLevel::warn:  return "warn";
        case LogLevel::info:  return "info";
        case LogLevel::debug: return "debug";
        case LogLevel::trace: return "trace";
    }
    return "info";
}

void Log::vlog(LogLevel level, const char* fmt, va_list ap)
{
    if (!enabled_ || static_cast<int>(level) > static_cast<int>(level_))
    {
        return;
    }

    char body[2048];
    va_list ap2;
    va_copy(ap2, ap);
    vsnprintf(body, sizeof(body), fmt, ap2);
    va_end(ap2);

    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm tm_utc{};
    gmtime_r(&ts.tv_sec, &tm_utc);
    char tbuf[64];
    snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
             ts.tv_nsec / 1000000L);

    fprintf(stderr, "zxem[%s] %s %s\n", tbuf, level_name(level), body);
    if (file_ != nullptr)
    {
        fprintf(file_, "zxem[%s] %s %s\n", tbuf, level_name(level), body);
        fflush(file_);
    }
}

void Log::log(LogLevel level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(level, fmt, ap);
    va_end(ap);
}

static void log_at(LogLevel level, const char* fmt, va_list ap)
{
    Log::instance().vlog(level, fmt, ap);
}

void log_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_at(LogLevel::error, fmt, ap);
    va_end(ap);
}

void log_warn(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_at(LogLevel::warn, fmt, ap);
    va_end(ap);
}

void log_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_at(LogLevel::info, fmt, ap);
    va_end(ap);
}

void log_debug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_at(LogLevel::debug, fmt, ap);
    va_end(ap);
}

void log_trace(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_at(LogLevel::trace, fmt, ap);
    va_end(ap);
}
