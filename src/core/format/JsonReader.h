#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace icd::core {

    // Malformed JSON, or a value of the wrong kind where a reader expected another; the message says where.
    class JsonError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // A parsed JSON value (RFC 8259) for the data files the app reads: the class catalog, strategy presets and
    // settings. Small documents only, so a plain tree is enough; objects keep their members in file order.
    class JsonValue {
    public:
        // An object's members in file order.
        using Members = std::vector<std::pair<std::string, JsonValue>>;

        // null.
        JsonValue() = default;

        // A value of a given kind.
        explicit JsonValue(std::variant<std::nullptr_t, bool, double, std::string, std::vector<JsonValue>, Members> v) :
            value_(std::move(v)) {}

        // Kind tests.
        [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
        [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(value_); } // true or false.
        [[nodiscard]] bool isNumber() const { return std::holds_alternative<double>(value_); } // Any number.
        [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(value_); } // A string.
        [[nodiscard]] bool isArray() const { return std::holds_alternative<std::vector<JsonValue>>(value_); } // [..].
        [[nodiscard]] bool isObject() const { return std::holds_alternative<Members>(value_); } // {..}.

        // The value as a bool; throws JsonError for another kind.
        [[nodiscard]] bool asBool() const;
        // The value as a number; throws JsonError for another kind.
        [[nodiscard]] double asNumber() const;
        // The value as a whole number in [0, 2^53]; throws JsonError for another kind, fractions and negatives.
        [[nodiscard]] std::uint64_t asUnsigned() const;
        // The value as a string; throws JsonError for another kind.
        [[nodiscard]] const std::string& asString() const;
        // The value as an array; throws JsonError for another kind.
        [[nodiscard]] const std::vector<JsonValue>& asArray() const;
        // The value as an object; throws JsonError for another kind.
        [[nodiscard]] const Members& asObject() const;

        // An object's member by key, or null when absent; throws JsonError when this is not an object.
        [[nodiscard]] const JsonValue* find(std::string_view key) const;
        // An object's member by key; throws JsonError when absent or this is not an object.
        [[nodiscard]] const JsonValue& at(std::string_view key) const;

    private:
        std::variant<std::nullptr_t, bool, double, std::string, std::vector<JsonValue>, Members> value_; // The value.
    };

    // Parses a whole JSON document (UTF-8; a leading byte-order mark is skipped). Throws JsonError with line and column
    // for malformed input, trailing text and nesting deeper than 64 levels.
    [[nodiscard]] JsonValue parseJson(std::string_view text);

} // namespace icd::core
