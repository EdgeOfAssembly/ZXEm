/**
 * @file log.h
 * @brief Reverse-engineering logger for ZXEm (stderr + optional file).
 */
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

/** @brief Log verbosity. Default product level is @c info. */
enum class LogLevel : int
{
    error = 0,
    warn  = 1,
    info  = 2,
    debug = 3,
    trace = 4
};

/**
 * @brief Process-wide RE log configuration.
 *
 * Logging is ON by default (info → stderr). Disable with --no-log.
 * Instruction/I/O traces are opt-in (very noisy).
 */
class Log
{
public:
    static Log& instance();

    void set_enabled(bool on);
    bool enabled() const;

    void set_level(LogLevel level);
    LogLevel level() const;

    /**
     * @brief Also write to @p path (created/truncated). Empty path closes the file.
     * @return false if the file could not be opened.
     */
    bool set_file(const char* path);

    void set_trace_cpu(bool on);
    void set_trace_io(bool on);
    bool trace_cpu() const;
    bool trace_io() const;

    void vlog(LogLevel level, const char* fmt, va_list ap);
    void log(LogLevel level, const char* fmt, ...);

    static LogLevel parse_level(const char* s, bool* ok = nullptr);
    static const char* level_name(LogLevel level);

private:
    Log();
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    bool enabled_ = true;
    bool trace_cpu_ = false;
    bool trace_io_ = false;
    LogLevel level_ = LogLevel::info;
    FILE* file_ = nullptr;
    std::string file_path_;
};

void log_error(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_debug(const char* fmt, ...);
void log_trace(const char* fmt, ...);
