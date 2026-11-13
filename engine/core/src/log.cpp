#include "bully/core/log.h"

#include <iostream>

namespace bully::core {

std::string_view to_string(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

std::uint64_t Logger::add_sink(LogSink sink)
{
    const std::uint64_t id = next_sink_id_++;
    sinks_.emplace_back(id, std::move(sink));
    return id;
}

void Logger::remove_sink(std::uint64_t id)
{
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(),
                                [id](const auto& entry) { return entry.first == id; }),
                 sinks_.end());
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message)
{
    if (level < min_level_ || level == LogLevel::Off) return;
    LogRecord record{level, std::string(category), std::string(message), sequence_++};
    for (auto& [id, sink] : sinks_) {
        // A misbehaving sink must never take the process down.
        try {
            if (sink) sink(record);
        } catch (...) {
        }
    }
}

LogSink Logger::stderr_sink()
{
    return [](const LogRecord& record) {
        std::cerr << '[' << to_string(record.level) << "] " << record.category << ": "
                  << record.message << '\n';
    };
}

}  // namespace bully::core
