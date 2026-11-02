#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace icd::core {

    // Minimal streaming JSON writer for machine-readable output (icd-cli --json). Writing only, so no JSON library
    // is needed yet; values are emitted in call order, two-space indented, with strings escaped per RFC 8259.
    // Misuse (a value without a key inside an object) is not checked: callers are our own report builders.
    class JsonWriter {
    public:
        // A writer of indented output.
        JsonWriter() = default;

        // A writer that puts everything on one line (JSON lines logs) when `oneLine` is true.
        explicit JsonWriter(bool oneLine) : oneLine_(oneLine) {}

        // Opens an object; as a member of an object pass its key first with key().
        JsonWriter& beginObject();
        // Closes the innermost object.
        JsonWriter& endObject();
        // Opens an array.
        JsonWriter& beginArray();
        // Closes the innermost array.
        JsonWriter& endArray();
        // Names the next value inside an object.
        JsonWriter& key(std::string_view name);
        // A string value (UTF-8 in, escaped out).
        JsonWriter& value(std::string_view text);
        // A string value from a C string (keeps literals from turning into bool).
        JsonWriter& value(const char* text) { return value(std::string_view(text)); }
        // An unsigned integer value.
        JsonWriter& value(std::uint64_t number);
        // A signed integer value.
        JsonWriter& value(std::int64_t number);
        // A number with up to six significant decimals (NaN and infinities become null).
        JsonWriter& value(double number);
        // true or false.
        JsonWriter& value(bool flag);
        // null.
        JsonWriter& null();

        // The text written so far.
        [[nodiscard]] const std::string& str() const { return out_; }

    private:
        // Separator and indentation before a value or key at the current depth.
        void beforeValue();
        // Writes the newline and indentation of the current depth.
        void indent();

        std::string out_; // Output.
        std::vector<bool> hasItems_; // Per open container: whether it has items yet.
        bool afterKey_ = false; // The next value follows a key on the same line.
        bool oneLine_ = false; // No newlines or indentation (one JSON value per line).
    };

    // A JSON string literal with quotes and escapes, for single values.
    [[nodiscard]] std::string jsonQuote(std::string_view text);

} // namespace icd::core
