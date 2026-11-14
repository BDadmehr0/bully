#include "bully/core/error.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bully::core {

std::string_view to_string(Severity severity) noexcept
{
    switch (severity) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
        case Severity::Fatal: return "fatal";
    }
    return "unknown";
}

std::string Diagnostic::format() const
{
    std::string out = "[";
    out += to_string(severity);
    out += "] ";
    if (!code.empty()) {
        out += code;
        out += ": ";
    }
    out += message;
    if (!source.empty()) {
        out += " (";
        out += source;
        if (line > 0) {
            out += ":";
            out += std::to_string(line);
        }
        out += ")";
    }
    return out;
}

void DiagnosticList::add(Severity severity, std::string code, std::string message,
                         std::string source, std::uint32_t line)
{
    items_.push_back(Diagnostic{severity, std::move(code), std::move(message), std::move(source), line});
}

void DiagnosticList::info(std::string code, std::string message, std::string source, std::uint32_t line)
{
    add(Severity::Info, std::move(code), std::move(message), std::move(source), line);
}

void DiagnosticList::warn(std::string code, std::string message, std::string source, std::uint32_t line)
{
    add(Severity::Warning, std::move(code), std::move(message), std::move(source), line);
}

void DiagnosticList::error(std::string code, std::string message, std::string source, std::uint32_t line)
{
    add(Severity::Error, std::move(code), std::move(message), std::move(source), line);
}

void DiagnosticList::append(const DiagnosticList& other)
{
    items_.insert(items_.end(), other.items_.begin(), other.items_.end());
}

void DiagnosticList::append(DiagnosticList&& other)
{
    items_.insert(items_.end(), std::make_move_iterator(other.items_.begin()),
                  std::make_move_iterator(other.items_.end()));
    other.items_.clear();
}

std::size_t DiagnosticList::count(Severity severity) const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(items_.begin(), items_.end(),
                      [severity](const Diagnostic& d) { return d.severity == severity; }));
}

std::size_t DiagnosticList::error_count() const noexcept
{
    return count(Severity::Error) + count(Severity::Fatal);
}

bool DiagnosticList::has_errors() const noexcept
{
    return std::any_of(items_.begin(), items_.end(), [](const Diagnostic& d) {
        return d.severity == Severity::Error || d.severity == Severity::Fatal;
    });
}

bool DiagnosticList::has_fatal() const noexcept
{
    return std::any_of(items_.begin(), items_.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Fatal; });
}

std::string DiagnosticList::format() const
{
    std::string out;
    for (const auto& item : items_) {
        out += item.format();
        out += '\n';
    }
    return out;
}

}  // namespace bully::core
