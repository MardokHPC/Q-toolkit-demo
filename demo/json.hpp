#pragma once

// JSON, ours: a writer with fixed separators and one value per line, and a
// strict reader. A third-party library would be a dependency for what this file
// does, and the artifacts' bytes are a contract (§3.6): a committed report must
// not change because a library updated its float formatting.
//
// Numbers never touch a locale: std::to_chars and std::from_chars only.

#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qtoolkit {

class JsonWriter {
public:
    void begin_object() { open('{'); }
    void end_object() { close(); }
    void begin_array() { open('['); }
    void end_array() { close(); }

    // One member of the enclosing object: the next value belongs to it.
    void field(std::string_view name) {
        Frame& frame = frames_.back();
        if (frame.items++ > 0) {
            out_ += ',';
        }
        newline(frames_.size());
        quote(name);
        out_ += ": ";
    }

    void integer(long long value) {
        value_slot();
        out_ += std::to_string(value);
    }

    void number(double value) {
        value_slot();
        char buffer[64] = {};
        const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc()) {
            throw std::logic_error("JsonWriter: a double did not fit in 64 characters");
        }
        out_.append(buffer, result.ptr);
    }

    void text(std::string_view value) {
        value_slot();
        quote(value);
    }

    void boolean(bool value) {
        value_slot();
        out_ += value ? "true" : "false";
    }

    [[nodiscard]] const std::string& str() const noexcept { return out_; }

private:
    struct Frame {
        char kind = '{';
        std::size_t items = 0;
    };

    void open(char kind) {
        value_slot();
        out_ += kind;
        frames_.push_back(Frame{kind, 0});
    }

    void close() {
        const Frame frame = frames_.back();
        frames_.pop_back();
        if (frame.items > 0) {
            newline(frames_.size());
        }
        out_ += frame.kind == '{' ? '}' : ']';
    }

    // An array element takes the comma and the break. At the top level there is
    // nothing before it; inside an object the comma is field()'s.
    void value_slot() {
        if (frames_.empty()) {
            return;
        }
        Frame& frame = frames_.back();
        if (frame.kind == '[') {
            if (frame.items++ > 0) {
                out_ += ',';
            }
            newline(frames_.size());
        }
    }

    void newline(std::size_t depth) {
        out_ += '\n';
        out_.append(depth * 2, ' ');
    }

    void quote(std::string_view value) {
        static constexpr char kHex[] = "0123456789abcdef";
        out_ += '"';
        for (const char raw : value) {
            const unsigned char byte = static_cast<unsigned char>(raw);
            switch (byte) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (byte < 0x20) {
                        out_ += "\\u00";
                        out_ += kHex[byte >> 4];
                        out_ += kHex[byte & 0x0F];
                    } else {
                        out_ += raw;
                    }
                    break;
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<Frame> frames_;
};

// ---------------------------------------------------------------------------
// Reader. Deliberately strict: unknown or repeated keys, a trailing byte, a
// lone surrogate are errors, because the one thing a report must not do is
// parse "almost".
// ---------------------------------------------------------------------------
struct JsonValue {
    enum class Kind { null_value, boolean, number, string, array, object };

    Kind kind = Kind::null_value;
    bool boolean = false;
    bool integral = false;  // the token had neither a fraction nor an exponent
    double number = 0.0;
    std::string token;  // a number's exact text, so it can be re-emitted verbatim
    std::string text;   // a string's value
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    [[nodiscard]] const JsonValue* find(std::string_view name) const noexcept {
        for (const auto& member : members) {
            if (member.first == name) {
                return &member.second;
            }
        }
        return nullptr;
    }
};

namespace detail {

inline void append_utf8(std::string& out, unsigned int code_point) {
    if (code_point < 0x80) {
        out += static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

class JsonReader {
public:
    explicit JsonReader(std::string_view document) : document_(document) {}

    bool parse(JsonValue& out) {
        out = JsonValue{};
        skip_space();
        if (!value(out, 0)) {
            return false;
        }
        skip_space();
        if (position_ != document_.size()) {
            return fail("trailing content after the document");
        }
        return true;
    }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    static constexpr int kMaxDepth = 32;

    bool fail(const std::string& message) {
        error_ = "offset " + std::to_string(position_) + ": " + message;
        return false;
    }

    void skip_space() {
        while (position_ < document_.size()) {
            const char current = document_[position_];
            if (current != ' ' && current != '\t' && current != '\n' && current != '\r') {
                return;
            }
            ++position_;
        }
    }

    bool literal(std::string_view word) {
        if (document_.substr(position_, word.size()) != word) {
            return false;
        }
        position_ += word.size();
        return true;
    }

    bool value(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            return fail("nesting is deeper than 32");
        }
        if (position_ >= document_.size()) {
            return fail("the document ends where a value was expected");
        }
        const char current = document_[position_];
        if (current == '{') {
            return object(out, depth);
        }
        if (current == '[') {
            return array(out, depth);
        }
        if (current == '"') {
            out.kind = JsonValue::Kind::string;
            return string(out.text);
        }
        if (current == 't') {
            out.kind = JsonValue::Kind::boolean;
            out.boolean = true;
            return literal("true") || fail("expected 'true'");
        }
        if (current == 'f') {
            out.kind = JsonValue::Kind::boolean;
            out.boolean = false;
            return literal("false") || fail("expected 'false'");
        }
        if (current == 'n') {
            out.kind = JsonValue::Kind::null_value;
            return literal("null") || fail("expected 'null'");
        }
        out.kind = JsonValue::Kind::number;
        return number(out);
    }

    bool number(JsonValue& out) {
        const std::size_t start = position_;
        if (position_ < document_.size() && document_[position_] == '-') {
            ++position_;
        }
        const std::size_t digits = position_;
        while (position_ < document_.size() && document_[position_] >= '0' &&
               document_[position_] <= '9') {
            ++position_;
        }
        if (position_ == digits) {
            return fail("a number needs at least one digit");
        }
        if (document_[digits] == '0' && position_ - digits > 1) {
            return fail("a number cannot have a leading zero");
        }
        out.integral = true;
        if (position_ < document_.size() && document_[position_] == '.') {
            out.integral = false;
            ++position_;
            const std::size_t fraction = position_;
            while (position_ < document_.size() && document_[position_] >= '0' &&
                   document_[position_] <= '9') {
                ++position_;
            }
            if (position_ == fraction) {
                return fail("a fraction needs at least one digit");
            }
        }
        if (position_ < document_.size() &&
            (document_[position_] == 'e' || document_[position_] == 'E')) {
            out.integral = false;
            ++position_;
            if (position_ < document_.size() &&
                (document_[position_] == '+' || document_[position_] == '-')) {
                ++position_;
            }
            const std::size_t exponent = position_;
            while (position_ < document_.size() && document_[position_] >= '0' &&
                   document_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponent) {
                return fail("an exponent needs at least one digit");
            }
        }

        out.token.assign(document_.substr(start, position_ - start));
        const char* first = out.token.data();
        const char* last = first + out.token.size();
        const std::from_chars_result result = std::from_chars(first, last, out.number);
        if (result.ec != std::errc() || result.ptr != last) {
            return fail("the number '" + out.token + "' does not fit a double");
        }
        return true;
    }

    static int hex_digit(char current) {
        if (current >= '0' && current <= '9') {
            return current - '0';
        }
        if (current >= 'a' && current <= 'f') {
            return current - 'a' + 10;
        }
        if (current >= 'A' && current <= 'F') {
            return current - 'A' + 10;
        }
        return -1;
    }

    bool hex4(unsigned int& out, std::string& what) {
        if (position_ + 4 > document_.size()) {
            what = "a \\u escape needs four hex digits";
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_digit(document_[position_ + static_cast<std::size_t>(i)]);
            if (digit < 0) {
                what = "a \\u escape needs four hex digits";
                return false;
            }
            out = (out << 4) | static_cast<unsigned int>(digit);
        }
        position_ += 4;
        return true;
    }

    bool string(std::string& out) {
        out.clear();
        ++position_;  // the opening quote
        while (true) {
            if (position_ >= document_.size()) {
                return fail("the string is not terminated");
            }
            const unsigned char byte = static_cast<unsigned char>(document_[position_]);
            if (byte == '"') {
                ++position_;
                return true;
            }
            if (byte < 0x20) {
                return fail("a control character inside a string");
            }
            if (byte != '\\') {
                out += static_cast<char>(byte);
                ++position_;
                continue;
            }

            ++position_;
            if (position_ >= document_.size()) {
                return fail("the string ends after a backslash");
            }
            const char escaped = document_[position_++];
            switch (escaped) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::string what;
                    unsigned int code_point = 0;
                    if (!hex4(code_point, what)) {
                        return fail(what);
                    }
                    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                        if (!literal("\\u")) {
                            return fail("a high surrogate without its low surrogate");
                        }
                        unsigned int low = 0;
                        if (!hex4(low, what)) {
                            return fail(what);
                        }
                        if (low < 0xDC00 || low > 0xDFFF) {
                            return fail("a high surrogate followed by something else");
                        }
                        code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                        return fail("a low surrogate without its high surrogate");
                    }
                    append_utf8(out, code_point);
                    break;
                }
                default:
                    return fail(std::string("unknown escape '\\") + escaped + "'");
            }
        }
    }

    bool array(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::array;
        ++position_;  // '['
        skip_space();
        if (position_ < document_.size() && document_[position_] == ']') {
            ++position_;
            return true;
        }
        while (true) {
            out.items.emplace_back();
            if (!value(out.items.back(), depth + 1)) {
                return false;
            }
            skip_space();
            if (position_ >= document_.size()) {
                return fail("the array is not terminated");
            }
            if (document_[position_] == ',') {
                ++position_;
                skip_space();
                continue;
            }
            if (document_[position_] == ']') {
                ++position_;
                return true;
            }
            return fail("an array needs a comma or a closing bracket");
        }
    }

    bool object(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::object;
        ++position_;  // '{'
        skip_space();
        if (position_ < document_.size() && document_[position_] == '}') {
            ++position_;
            return true;
        }
        while (true) {
            skip_space();
            if (position_ >= document_.size() || document_[position_] != '"') {
                return fail("an object key must be a string");
            }
            std::string key;
            if (!string(key)) {
                return false;
            }
            if (out.find(key) != nullptr) {
                return fail("duplicate key '" + key + "'");
            }
            skip_space();
            if (position_ >= document_.size() || document_[position_] != ':') {
                return fail("a key needs a colon after it");
            }
            ++position_;
            skip_space();
            out.members.emplace_back(std::move(key), JsonValue{});
            if (!value(out.members.back().second, depth + 1)) {
                return false;
            }
            skip_space();
            if (position_ >= document_.size()) {
                return fail("the object is not terminated");
            }
            if (document_[position_] == ',') {
                ++position_;
                continue;
            }
            if (document_[position_] == '}') {
                ++position_;
                return true;
            }
            return fail("an object needs a comma or a closing brace");
        }
    }

    std::string_view document_;
    std::size_t position_ = 0;
    std::string error_;
};

}  // namespace detail

[[nodiscard]] inline bool parse_json(std::string_view document, JsonValue& out,
                                     std::string& error) {
    detail::JsonReader reader(document);
    if (!reader.parse(out)) {
        error = reader.error();
        return false;
    }
    error.clear();
    return true;
}

}  // namespace qtoolkit
