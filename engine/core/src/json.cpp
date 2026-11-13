#include "bully/core/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "bully/core/fs.h"
#include "bully/core/strings.h"

namespace bully::core::json {

Value::Value(ValueList array) : type_(Type::Array), array_(std::move(array)) {}
Value::Value(Object object) : type_(Type::Object), object_(std::move(object)) {}

Value Value::make_array() { return Value(ValueList{}); }
Value Value::make_object() { return Value(Object{}); }

// ---------------------------------------------------------------- Object ----

bool Object::has(std::string_view key) const { return find(key) != nullptr; }

const Value* Object::find(std::string_view key) const
{
    for (const auto& [entry_key, entry_value] : entries_) {
        if (entry_key == key) return &entry_value;
    }
    return nullptr;
}

Value* Object::find(std::string_view key)
{
    for (auto& [entry_key, entry_value] : entries_) {
        if (entry_key == key) return &entry_value;
    }
    return nullptr;
}

Value& Object::set(std::string key, Value value)
{
    if (Value* existing = find(key)) {
        *existing = std::move(value);
        return *existing;
    }
    entries_.emplace_back(std::move(key), std::move(value));
    return entries_.back().second;
}

bool Object::erase(std::string_view key)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == key) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------- Value ----

bool Value::as_bool(bool fallback) const noexcept
{
    if (is_bool()) return bool_;
    if (is_number()) return number_ != 0.0;
    return fallback;
}

double Value::as_number(double fallback) const noexcept
{
    if (is_number()) return number_;
    if (is_bool()) return bool_ ? 1.0 : 0.0;
    return fallback;
}

std::int64_t Value::as_int(std::int64_t fallback) const noexcept
{
    if (is_number()) return static_cast<std::int64_t>(std::llround(number_));
    return fallback;
}

std::string Value::as_string(std::string fallback) const noexcept
{
    if (is_string()) return string_;
    return fallback;
}

std::string Value::get_string(std::string_view key, std::string fallback) const
{
    const Value* found = find(key);
    return found ? found->as_string(std::move(fallback)) : fallback;
}

double Value::get_number(std::string_view key, double fallback) const
{
    const Value* found = find(key);
    return found ? found->as_number(fallback) : fallback;
}

std::int64_t Value::get_int(std::string_view key, std::int64_t fallback) const
{
    const Value* found = find(key);
    return found ? found->as_int(fallback) : fallback;
}

bool Value::get_bool(std::string_view key, bool fallback) const
{
    const Value* found = find(key);
    return found ? found->as_bool(fallback) : fallback;
}

Value& Value::operator[](std::string_view key)
{
    if (!is_object()) {
        type_ = Type::Object;
        object_ = Object{};
    }
    if (Value* existing = object_.find(key)) return *existing;
    return object_.set(std::string(key), Value{});
}

const Value& Value::at(std::string_view key) const
{
    static const Value kNull;
    const Value* found = find(key);
    return found ? *found : kNull;
}

const Value* Value::find(std::string_view key) const
{
    return is_object() ? object_.find(key) : nullptr;
}

bool Value::contains(std::string_view key) const { return find(key) != nullptr; }

void Value::push(Value value)
{
    if (!is_array()) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

std::size_t Value::size() const noexcept
{
    if (is_array()) return array_.size();
    if (is_object()) return object_.size();
    return 0;
}

const Value& Value::operator[](std::size_t index) const
{
    static const Value kNull;
    if (!is_array() || index >= array_.size()) return kNull;
    return array_[index];
}

Value& Value::operator[](std::size_t index)
{
    if (!is_array()) {
        type_ = Type::Array;
        array_.clear();
    }
    if (index >= array_.size()) array_.resize(index + 1);
    return array_[index];
}

// ------------------------------------------------------------ Serialising ---

std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

namespace {

void write_number(std::string& out, double number)
{
    if (!std::isfinite(number)) {
        out += "0";
        return;
    }
    if (number == std::floor(number) && std::fabs(number) < 1e15) {
        char buffer[32];
        const auto value = static_cast<long long>(number);
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out.append(buffer, result.ptr);
        return;
    }
    char buffer[40];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number,
                                      std::chars_format::general, 17);
    out.append(buffer, result.ptr);
}

void write_indent(std::string& out, int indent, int depth)
{
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent * depth), ' ');
}

}  // namespace

void Value::write(std::string& out, int indent, int depth) const
{
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: write_number(out, number_); break;
        case Type::String:
            out.push_back('"');
            out += escape(string_);
            out.push_back('"');
            break;
        case Type::Array: {
            if (array_.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            bool first = true;
            for (const auto& item : array_) {
                if (!first) out.push_back(',');
                first = false;
                write_indent(out, indent, depth + 1);
                item.write(out, indent, depth + 1);
            }
            write_indent(out, indent, depth);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (object_.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : object_.entries()) {
                if (!first) out.push_back(',');
                first = false;
                write_indent(out, indent, depth + 1);
                out.push_back('"');
                out += escape(key);
                out += indent > 0 ? "\": " : "\":";
                item.write(out, indent, depth + 1);
            }
            write_indent(out, indent, depth);
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump(int indent) const
{
    std::string out;
    write(out, indent, 0);
    return out;
}

// ---------------------------------------------------------------- Parsing ---

namespace {

class Parser {
public:
    Parser(std::string_view text, DiagnosticList& diagnostics)
        : text_(text), diagnostics_(diagnostics) {}

    Result<Value> parse()
    {
        skip_whitespace();
        auto value = parse_value(0);
        if (!value) return value;
        skip_whitespace();
        if (position_ != text_.size()) {
            fail("BULLY-JSON-0002", "unexpected trailing content");
            return Result<Value>(std::move(diagnostics_));
        }
        return value;
    }

private:
    [[nodiscard]] bool at_end() const noexcept { return position_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : text_[position_]; }

    void skip_whitespace()
    {
        while (!at_end()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (c == '\n') {
                    ++line_;
                    column_ = 1;
                } else {
                    ++column_;
                }
                ++position_;
            } else {
                break;
            }
        }
    }

    void fail(std::string code, std::string message)
    {
        diagnostics_.error(std::move(code), std::move(message), "json", static_cast<std::uint32_t>(line_));
    }

    Result<Value> parse_value(int depth)
    {
        if (depth > 200) {
            fail("BULLY-JSON-0003", "nesting depth exceeds 200");
            return Result<Value>(std::move(diagnostics_));
        }
        skip_whitespace();
        if (at_end()) {
            fail("BULLY-JSON-0001", "unexpected end of input");
            return Result<Value>(std::move(diagnostics_));
        }
        switch (peek()) {
            case '{': return parse_object(depth);
            case '[': return parse_array(depth);
            case '"': {
                auto text = parse_string();
                if (!text) return Result<Value>(std::move(diagnostics_));
                return Value(std::move(text).value());
            }
            case 't': return parse_literal("true", Value(true));
            case 'f': return parse_literal("false", Value(false));
            case 'n': return parse_literal("null", Value(nullptr));
            default: return parse_number();
        }
    }

    Result<Value> parse_literal(std::string_view literal, Value value)
    {
        if (text_.compare(position_, literal.size(), literal) != 0) {
            fail("BULLY-JSON-0004", "invalid literal, expected '" + std::string(literal) + "'");
            return Result<Value>(std::move(diagnostics_));
        }
        position_ += literal.size();
        column_ += static_cast<int>(literal.size());
        return value;
    }

    Result<Value> parse_number()
    {
        const std::size_t start = position_;
        if (peek() == '-') ++position_;
        while (!at_end()) {
            const char c = text_[position_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++position_;
            } else {
                break;
            }
        }
        const std::string token(text_.substr(start, position_ - start));
        if (token.empty()) {
            fail("BULLY-JSON-0005", "unexpected character '" + std::string(1, peek()) + "'");
            return Result<Value>(std::move(diagnostics_));
        }
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            fail("BULLY-JSON-0005", "malformed number '" + token + "'");
            return Result<Value>(std::move(diagnostics_));
        }
        return Value(number);
    }

    Result<std::string> parse_string()
    {
        if (peek() != '"') {
            fail("BULLY-JSON-0006", "expected '\"'");
            return Result<std::string>(std::move(diagnostics_));
        }
        ++position_;
        std::string out;
        while (!at_end()) {
            const char c = text_[position_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (at_end()) break;
                const char esc = text_[position_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (position_ + 4 > text_.size()) {
                            fail("BULLY-JSON-0007", "truncated \\u escape");
                            return Result<std::string>(std::move(diagnostics_));
                        }
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = text_[position_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else {
                                fail("BULLY-JSON-0007", "invalid \\u escape");
                                return Result<std::string>(std::move(diagnostics_));
                            }
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default:
                        fail("BULLY-JSON-0007", std::string("invalid escape '\\") + esc + "'");
                        return Result<std::string>(std::move(diagnostics_));
                }
            } else {
                out.push_back(c);
            }
        }
        fail("BULLY-JSON-0008", "unterminated string");
        return Result<std::string>(std::move(diagnostics_));
    }

    static void append_utf8(std::string& out, unsigned code)
    {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    Result<Value> parse_object(int depth)
    {
        ++position_;  // '{'
        Value result = Value::make_object();
        skip_whitespace();
        if (peek() == '}') {
            ++position_;
            return result;
        }
        while (true) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) return Result<Value>(std::move(diagnostics_));
            skip_whitespace();
            if (peek() != ':') {
                fail("BULLY-JSON-0009", "expected ':' after object key");
                return Result<Value>(std::move(diagnostics_));
            }
            ++position_;
            auto value = parse_value(depth + 1);
            if (!value) return value;
            result[std::move(key).value()] = std::move(value).value();
            skip_whitespace();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == '}') {
                ++position_;
                return result;
            }
            fail("BULLY-JSON-0009", "expected ',' or '}' in object");
            return Result<Value>(std::move(diagnostics_));
        }
    }

    Result<Value> parse_array(int depth)
    {
        ++position_;  // '['
        Value result = Value::make_array();
        skip_whitespace();
        if (peek() == ']') {
            ++position_;
            return result;
        }
        while (true) {
            auto value = parse_value(depth + 1);
            if (!value) return value;
            result.push(std::move(value).value());
            skip_whitespace();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == ']') {
                ++position_;
                return result;
            }
            fail("BULLY-JSON-0009", "expected ',' or ']' in array");
            return Result<Value>(std::move(diagnostics_));
        }
    }

    std::string_view text_;
    DiagnosticList& diagnostics_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

}  // namespace

Result<Value> parse(std::string_view text)
{
    // Tolerate a UTF-8 BOM.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    DiagnosticList diagnostics;
    Parser parser(text, diagnostics);
    auto result = parser.parse();
    if (!result) {
        if (diagnostics.empty()) diagnostics.error("BULLY-JSON-0001", "invalid JSON document");
        return Result<Value>(std::move(diagnostics));
    }
    return result;
}

}  // namespace bully::core::json

// ------------------------------------------------------------------ Files ---

namespace bully::core::json {

Result<Value> load_file(const std::string& path)
{
    auto text = FileSystem::read_text(path);
    if (!text) return Result<Value>(text.diagnostics());
    return parse(text.value());
}

Status save_file(const std::string& path, const Value& value, int indent)
{
    return FileSystem::write_text(path, value.dump(indent) + "\n");
}

}  // namespace bully::core::json
