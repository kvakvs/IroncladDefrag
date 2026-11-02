#include "core/format/JsonReader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace icd::core {

    namespace {

        // Deepest nesting accepted; the data files need a handful of levels.
        constexpr int maxDepth = 64;

        // Largest whole number a double holds exactly.
        constexpr double maxExact = 9007199254740992.0;

        // Name of a value's kind, for messages.
        const char* kindName(const JsonValue& v) {
            if (v.isNull()) {
                return "null";
            }
            if (v.isBool()) {
                return "a boolean";
            }
            if (v.isNumber()) {
                return "a number";
            }
            if (v.isString()) {
                return "a string";
            }
            return v.isArray() ? "an array" : "an object";
        }

        // Throws for a value of the wrong kind.
        [[noreturn]] void wrongKind(const JsonValue& v, const char* wanted) {
            throw JsonError(std::format("expected {}, found {}", wanted, kindName(v)));
        }

        // Appends a code point as UTF-8.
        void appendUtf8(std::uint32_t c, std::string& out) {
            if (c < 0x80) {
                out += static_cast<char>(c);
            }
            else if (c < 0x800) {
                out += static_cast<char>(0xC0 | (c >> 6U));
                out += static_cast<char>(0x80 | (c & 0x3FU));
            }
            else if (c < 0x10000) {
                out += static_cast<char>(0xE0 | (c >> 12U));
                out += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
                out += static_cast<char>(0x80 | (c & 0x3FU));
            }
            else {
                out += static_cast<char>(0xF0 | (c >> 18U));
                out += static_cast<char>(0x80 | ((c >> 12U) & 0x3FU));
                out += static_cast<char>(0x80 | ((c >> 6U) & 0x3FU));
                out += static_cast<char>(0x80 | (c & 0x3FU));
            }
        }

        // A recursive-descent parser over one document.
        class Parser {
        public:
            explicit Parser(std::string_view text) : text_(text) {
                if (text_.starts_with("\xEF\xBB\xBF")) {
                    pos_ = 3;
                }
            }

            // The whole document: one value and nothing after it.
            JsonValue document() {
                JsonValue v = value();
                skipSpace();
                if (pos_ != text_.size()) {
                    fail("unexpected text after the value");
                }
                return v;
            }

        private:
            // Throws with the line and column of the current position.
            [[noreturn]] void fail(std::string_view what) const {
                const std::string_view before = text_.substr(0, pos_);
                const auto line = 1 + std::ranges::count(before, '\n');
                const std::size_t lineStart = before.rfind('\n');
                const std::size_t column = pos_ - (lineStart == std::string_view::npos ? 0 : lineStart + 1) + 1;
                throw JsonError(std::format("JSON line {}, column {}: {}", line, column, what));
            }

            // Skips whitespace.
            void skipSpace() {
                while (pos_ < text_.size() &&
                       (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
                    ++pos_;
                }
            }

            // The next character without taking it ('\0' at the end).
            [[nodiscard]] char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

            // Takes an expected character.
            void expect(char c) {
                if (peek() != c) {
                    fail(std::format("expected '{}'", c));
                }
                ++pos_;
            }

            // Any value.
            JsonValue value() {
                skipSpace();
                switch (peek()) {
                case '{':
                    return nested([this] { return object(); });
                case '[':
                    return nested([this] { return array(); });
                case '"':
                    return JsonValue(string());
                case 't':
                case 'f':
                case 'n':
                    return literal();
                default:
                    return number();
                }
            }

            // A container one level deeper.
            template <typename Fn>
            JsonValue nested(Fn fn) {
                if (++depth_ > maxDepth) {
                    fail("nested too deeply");
                }
                JsonValue v = fn();
                --depth_;
                return v;
            }

            // An object.
            JsonValue object() {
                expect('{');
                JsonValue::Members members;
                skipSpace();
                if (peek() == '}') {
                    ++pos_;
                    return JsonValue(std::move(members));
                }
                do {
                    skipSpace();
                    std::string key = string();
                    skipSpace();
                    expect(':');
                    members.emplace_back(std::move(key), value());
                    skipSpace();
                }
                while (peek() == ',' && (++pos_, true));
                expect('}');
                return JsonValue(std::move(members));
            }

            // An array.
            JsonValue array() {
                expect('[');
                std::vector<JsonValue> items;
                skipSpace();
                if (peek() == ']') {
                    ++pos_;
                    return JsonValue(std::move(items));
                }
                do {
                    items.push_back(value());
                    skipSpace();
                }
                while (peek() == ',' && (++pos_, true));
                expect(']');
                return JsonValue(std::move(items));
            }

            // true, false or null.
            JsonValue literal() {
                for (const auto& [word, v] : {std::pair{"true", JsonValue(true)}, std::pair{"false", JsonValue(false)},
                                              std::pair{"null", JsonValue()}}) {
                    if (text_.substr(pos_).starts_with(word)) {
                        pos_ += std::string_view(word).size();
                        return v;
                    }
                }
                fail("unknown literal");
            }

            // A number (RFC 8259 grammar: no leading zeros, no leading '+', no bare '.').
            JsonValue number() {
                const std::size_t start = pos_;
                pos_ += peek() == '-' ? 1 : 0;
                const bool leadingZero = peek() == '0' && isDigit(pos_ + 1 < text_.size() ? text_[pos_ + 1] : '\0');
                if (!isDigit(peek()) || leadingZero) {
                    fail("expected a value");
                }
                while (pos_ < text_.size() && isNumberChar(text_[pos_])) {
                    ++pos_;
                }
                return JsonValue(converted(start));
            }

            // The number from `start` to the current position.
            double converted(std::size_t start) {
                double out = 0;
                const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + pos_, out);
                const bool whole = error == std::errc() && end == text_.data() + pos_;
                if (!whole || !std::isfinite(out) || !digitsFollow(text_.substr(start, pos_ - start))) {
                    pos_ = start;
                    fail("malformed number");
                }
                return out;
            }

            // Whether a digit is at position i.
            static bool digitAt(std::string_view s, std::size_t i) { return i < s.size() && isDigit(s[i]); }

            // Whether every '.' and exponent in a number is followed by a digit (from_chars takes "1." and "1e").
            static bool digitsFollow(std::string_view number) {
                for (std::size_t i = 0; i < number.size(); ++i) {
                    const char c = number[i];
                    const bool exponent = c == 'e' || c == 'E';
                    const bool sign = exponent && digitAt(number, i + 2) && !digitAt(number, i + 1);
                    if ((c == '.' || exponent) && !digitAt(number, sign ? i + 2 : i + 1)) {
                        return false;
                    }
                }
                return true;
            }

            // Whether a character is a decimal digit.
            static bool isDigit(char c) { return c >= '0' && c <= '9'; }

            // Whether a character can appear in a number.
            static bool isNumberChar(char c) {
                return isDigit(c) || std::string_view("+-.eE").find(c) != std::string_view::npos;
            }

            // Four hex digits of a \u escape.
            std::uint32_t hex4() {
                std::uint32_t c = 0;
                const auto [end, error] =
                    std::from_chars(text_.data() + pos_, text_.data() + std::min(pos_ + 4, text_.size()), c, 16);
                if (error != std::errc() || end != text_.data() + pos_ + 4) {
                    fail("bad \\u escape");
                }
                pos_ += 4;
                return c;
            }

            // The code point of a \u escape (a surrogate pair takes two).
            std::uint32_t unicodeEscape() {
                const std::uint32_t c = hex4();
                if (c < 0xD800 || c > 0xDFFF) {
                    return c;
                }
                if (c > 0xDBFF || !text_.substr(pos_).starts_with("\\u")) {
                    fail("unpaired surrogate");
                }
                pos_ += 2;
                const std::uint32_t low = hex4();
                if (low < 0xDC00 || low > 0xDFFF) {
                    fail("unpaired surrogate");
                }
                return 0x10000 + ((c - 0xD800) << 10U) + (low - 0xDC00);
            }

            // One escape after the backslash.
            void escape(std::string& out) {
                const char c = peek();
                ++pos_;
                const std::string_view simple = "\"\\/bfnrt";
                const std::string_view meaning = "\"\\/\b\f\n\r\t";
                if (const std::size_t i = simple.find(c); i != std::string_view::npos && c != '\0') {
                    out += meaning[i];
                }
                else if (c == 'u') {
                    appendUtf8(unicodeEscape(), out);
                }
                else {
                    --pos_;
                    fail("bad escape");
                }
            }

            // A string.
            std::string string() {
                expect('"');
                std::string out;
                while (peek() != '"') {
                    const char c = peek();
                    if (pos_ >= text_.size() || static_cast<unsigned char>(c) < 0x20) {
                        fail(pos_ >= text_.size() ? "unterminated string" : "control character in a string");
                    }
                    ++pos_;
                    if (c == '\\') {
                        escape(out);
                    }
                    else {
                        out += c;
                    }
                }
                ++pos_;
                return out;
            }

            std::string_view text_; // The document.
            std::size_t pos_ = 0; // Next character.
            int depth_ = 0; // Open containers.
        };

    } // namespace

    bool JsonValue::asBool() const {
        if (!isBool()) {
            wrongKind(*this, "a boolean");
        }
        return std::get<bool>(value_);
    }

    double JsonValue::asNumber() const {
        if (!isNumber()) {
            wrongKind(*this, "a number");
        }
        return std::get<double>(value_);
    }

    std::uint64_t JsonValue::asUnsigned() const {
        const double d = asNumber();
        if (d < 0 || d > maxExact || std::floor(d) != d) {
            throw JsonError(std::format("expected a whole number from 0 to 2^53, found {}", d));
        }
        return static_cast<std::uint64_t>(d);
    }

    const std::string& JsonValue::asString() const {
        if (!isString()) {
            wrongKind(*this, "a string");
        }
        return std::get<std::string>(value_);
    }

    const std::vector<JsonValue>& JsonValue::asArray() const {
        if (!isArray()) {
            wrongKind(*this, "an array");
        }
        return std::get<std::vector<JsonValue>>(value_);
    }

    const JsonValue::Members& JsonValue::asObject() const {
        if (!isObject()) {
            wrongKind(*this, "an object");
        }
        return std::get<Members>(value_);
    }

    const JsonValue* JsonValue::find(std::string_view key) const {
        const Members& members = asObject();
        const auto it = std::ranges::find(members, key, &Members::value_type::first);
        return it == members.end() ? nullptr : &it->second;
    }

    const JsonValue& JsonValue::at(std::string_view key) const {
        const JsonValue* v = find(key);
        if (v == nullptr) {
            throw JsonError(std::format("missing \"{}\"", key));
        }
        return *v;
    }

    JsonValue parseJson(std::string_view text) { return Parser(text).document(); }

} // namespace icd::core
