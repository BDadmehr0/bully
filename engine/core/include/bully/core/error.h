// Bully Studio - error reporting primitives.
//
// Design rule (from the platform requirements): no silent failures. Every
// fallible operation returns a Result or fills a Diagnostic list. Diagnostics
// carry a stable machine-readable code, severity, source location and a
// human-readable message, so the QA dashboard can aggregate them.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bully::core {

enum class Severity { Info, Warning, Error, Fatal };

[[nodiscard]] std::string_view to_string(Severity severity) noexcept;

/// A single diagnostic message.
struct Diagnostic {
    Severity severity = Severity::Error;
    /// Stable, greppable code, e.g. "BULLY-FMT-0007". Documented per module.
    std::string code;
    std::string message;
    /// Optional source reference: file path, asset id, node path or URL.
    std::string source;
    /// Optional 1-based line number (0 = not applicable).
    std::uint32_t line = 0;

    [[nodiscard]] std::string format() const;
};

/// Ordered collection of diagnostics with filtering helpers.
class DiagnosticList {
public:
    void add(Severity severity, std::string code, std::string message,
             std::string source = {}, std::uint32_t line = 0);

    void info(std::string code, std::string message, std::string source = {}, std::uint32_t line = 0);
    void warn(std::string code, std::string message, std::string source = {}, std::uint32_t line = 0);
    void error(std::string code, std::string message, std::string source = {}, std::uint32_t line = 0);

    void append(const DiagnosticList& other);
    void append(DiagnosticList&& other);

    [[nodiscard]] const std::vector<Diagnostic>& all() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] std::size_t count(Severity severity) const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool has_fatal() const noexcept;

    /// Human-readable multi-line report, one diagnostic per line.
    [[nodiscard]] std::string format() const;

    void clear() noexcept { items_.clear(); }

private:
    std::vector<Diagnostic> items_;
};

/// Result<T>: either a value or a DiagnosticList. Never throws for expected
/// failure modes (I/O, validation, version mismatch).
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : value_(std::move(value)), ok_(true) {}
    Result(DiagnosticList diagnostics) : diagnostics_(std::move(diagnostics)), ok_(false) {}

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] T& value() & { return value_; }
    [[nodiscard]] const T& value() const& { return value_; }
    [[nodiscard]] T&& value() && { return std::move(value_); }

    [[nodiscard]] const DiagnosticList& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] DiagnosticList& diagnostics() noexcept { return diagnostics_; }

    /// Attaches non-fatal diagnostics (warnings, informational notes) to a
    /// successful result. Readers use this to surface forward-compatibility
    /// warnings - a warning must never be dropped just because decoding worked.
    Result& attach_diagnostics(DiagnosticList extra) noexcept
    {
        diagnostics_.append(std::move(extra));
        return *this;
    }

private:
    T value_{};
    DiagnosticList diagnostics_;
    bool ok_ = false;
};

/// Result<void> specialisation.
template <>
class [[nodiscard]] Result<void> {
public:
    Result() : ok_(true) {}
    Result(DiagnosticList diagnostics) : diagnostics_(std::move(diagnostics)), ok_(false) {}

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] const DiagnosticList& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] DiagnosticList& diagnostics() noexcept { return diagnostics_; }

    /// Attaches non-fatal diagnostics to a successful status: validation
    /// routines report warnings and still succeed, and callers must be able to
    /// read them (the QA dashboard aggregates exactly these).
    Result& attach_diagnostics(DiagnosticList extra) noexcept
    {
        diagnostics_.append(std::move(extra));
        return *this;
    }

private:
    DiagnosticList diagnostics_;
    bool ok_ = false;
};

using Status = Result<void>;

}  // namespace bully::core
