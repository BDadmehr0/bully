// Bully Studio - logging.
//
// A tiny, dependency-free logger with pluggable sinks. The Studio console
// panel and the QA dashboard both attach sinks; the headless CLI tools attach
// a stderr sink. Logging must never be the reason a build fails, so all sink
// callbacks are invoked defensively.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/error.h"

namespace bully::core {

enum class LogLevel : std::uint8_t { Trace = 0, Debug, Info, Warn, Error, Off };

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;  // e.g. "pipeline", "renderer", "plugin:blender_bridge"
    std::string message;
    std::uint64_t sequence = 0;
};

using LogSink = std::function<void(const LogRecord&)>;

class Logger {
public:
    static Logger& instance();

    void set_min_level(LogLevel level) noexcept { min_level_ = level; }
    [[nodiscard]] LogLevel min_level() const noexcept { return min_level_; }

    /// Returns an id usable with remove_sink().
    std::uint64_t add_sink(LogSink sink);
    void remove_sink(std::uint64_t id);

    void log(LogLevel level, std::string_view category, std::string_view message);

    /// Convenience sink writing "[LEVEL] category: message" to stderr.
    static LogSink stderr_sink();

private:
    Logger() = default;
    std::vector<std::pair<std::uint64_t, LogSink>> sinks_;
    std::uint64_t next_sink_id_ = 1;
    std::uint64_t sequence_ = 0;
    LogLevel min_level_ = LogLevel::Info;
};

inline void log(LogLevel level, std::string_view category, std::string_view message)
{
    Logger::instance().log(level, category, message);
}
inline void log_trace(std::string_view c, std::string_view m) { log(LogLevel::Trace, c, m); }
inline void log_debug(std::string_view c, std::string_view m) { log(LogLevel::Debug, c, m); }
inline void log_info(std::string_view c, std::string_view m) { log(LogLevel::Info, c, m); }
inline void log_warn(std::string_view c, std::string_view m) { log(LogLevel::Warn, c, m); }
inline void log_error(std::string_view c, std::string_view m) { log(LogLevel::Error, c, m); }

}  // namespace bully::core
