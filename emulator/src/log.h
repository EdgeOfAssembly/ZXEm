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
 *
 * Optional log-file buffering: every line is written immediately, but
 * @c fflush is applied at once for @c error and @c warn, every 256 lines
 * for @c info / @c debug / @c trace, and on file close / destructor so the
 * tail is not lost. stderr is not extra-flushed (a TTY is already
 * line-buffered on newline).
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
     * @note Closing flushes any buffered @c info / @c debug / @c trace lines.
     */
    bool set_file(const char* path);

    void set_trace_cpu(bool on);
    void set_trace_io(bool on);
    bool trace_cpu() const;
    bool trace_io() const;

    /**
     * @brief Format and emit one log line to stderr and the optional file.
     * @note File @c fflush is immediate for @c error / @c warn; batched
     *       (256 lines) for lower levels. stderr is not extra-flushed.
     */
    void vlog(LogLevel level, const char* fmt, va_list ap);
    void log(LogLevel level, const char* fmt, ...);

    static LogLevel parse_level(const char* s, bool* ok = nullptr);
    static const char* level_name(LogLevel level);

private:
    Log();
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void close_file();

    bool enabled_ = true;
    bool trace_cpu_ = false;
    bool trace_io_ = false;
    LogLevel level_ = LogLevel::info;
    FILE* file_ = nullptr;
    std::string file_path_;
    unsigned unflushed_lines_ = 0;
    static constexpr unsigned kFileFlushInterval = 256;
};

void log_error(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_debug(const char* fmt, ...);
void log_trace(const char* fmt, ...);
