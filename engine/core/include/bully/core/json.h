// Bully Studio - minimal JSON value model + parser/writer.
//
// The platform targets Qt 6, but the engine, asset pipeline, CLI tools and
// tests must build and run headless without Qt. Every configuration file the
// data layer owns (.bullyproj, plugin manifests, import settings, transport
// files) therefore uses this self-contained, dependency-free JSON
// implementation. Object key order and number formatting are stable and
// byte-for-byte deterministic, which keeps compiled sidecars diff-friendly.
//
// Diagnostic codes: BULLY-JSON-0001..0009
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <type_traits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/error.h"

namespace bully::core::json {

class Value;

using ValueList = std::vector<Value>;

/// Object storage keeps insertion order so serialisation is deterministic.
class Object {
public:
    using Entry = std::pair<std::string, Value>;
    using Container = std::vector<Entry>;

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value* find(std::string_view key);
    Value& set(std::string key, Value value);
    [[nodiscard]] bool erase(std::string_view key);
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const Container& entries() const noexcept { return entries_; }
    [[nodiscard]] Container& entries() noexcept { return entries_; }

private:
    Container entries_;
};

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : type_(Type::Bool), bool_(value) {}
    Value(double value) : type_(Type::Number), number_(value) {}
    Value(float value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    /// Any other integral type maps to a JSON number without overload
    /// ambiguity (uint32_t, size_t, int64_t, ... all work).
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Value(T value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    Value(std::string value) : type_(Type::String), string_(std::move(value)) {}
    Value(const char* value) : type_(Type::String), string_(value) {}
    Value(ValueList array);
    Value(Object object);

    [[nodiscard]] static Value make_array();
    [[nodiscard]] static Value make_object();

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Object; }

    [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
    [[nodiscard]] double as_number(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] std::string as_string(std::string fallback = {}) const noexcept;

    /// Typed accessors with fallback - no exceptions, no silent type confusion.
    [[nodiscard]] std::string get_string(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] double get_number(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] std::int64_t get_int(std::string_view key, std::int64_t fallback = 0) const;
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback = false) const;

    /// Object element access; creates an object if this value is null.
    Value& operator[](std::string_view key);
    [[nodiscard]] const Value& at(std::string_view key) const;
    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;

    /// Array access.
    void push(Value value);
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const Value& operator[](std::size_t index) const;
    Value& operator[](std::size_t index);
    [[nodiscard]] const std::vector<Value>& items() const noexcept { return array_; }
    [[nodiscard]] std::vector<Value>& items() noexcept { return array_; }

    [[nodiscard]] const Object& object() const noexcept { return object_; }
    [[nodiscard]] Object& object() noexcept { return object_; }

    /// Serialise. `indent` 0 = compact single line.
    [[nodiscard]] std::string dump(int indent = 2) const;

    [[nodiscard]] auto operator<=>(const Value&) const = default;

private:
    void write(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    Object object_;
};

/// Parses JSON text. On failure returns diagnostics with line/column info.
[[nodiscard]] Result<Value> parse(std::string_view text);
[[nodiscard]] Result<Value> load_file(const std::string& path);
[[nodiscard]] Status save_file(const std::string& path, const Value& value, int indent = 2);

/// Escapes a string for embedding in JSON.
[[nodiscard]] std::string escape(std::string_view text);

}  // namespace bully::core::json
