#include "core/format/Json.h"

#include <cmath>
#include <format>

namespace icd::core {

    std::string jsonQuote(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        out.push_back('"');
        for (const char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                }
                else {
                    out.push_back(c); // UTF-8 bytes pass through unchanged.
                }
            }
        }
        out.push_back('"');
        return out;
    }

    void JsonWriter::indent() {
        out_.push_back('\n');
        out_.append(hasItems_.size() * 2, ' ');
    }

    void JsonWriter::beforeValue() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!hasItems_.empty()) {
            if (hasItems_.back()) {
                out_.push_back(',');
            }
            hasItems_.back() = true;
            indent();
        }
    }

    JsonWriter& JsonWriter::beginObject() {
        beforeValue();
        out_.push_back('{');
        hasItems_.push_back(false);
        return *this;
    }

    JsonWriter& JsonWriter::endObject() {
        const bool items = hasItems_.back();
        hasItems_.pop_back();
        if (items) {
            indent();
        }
        out_.push_back('}');
        return *this;
    }

    JsonWriter& JsonWriter::beginArray() {
        beforeValue();
        out_.push_back('[');
        hasItems_.push_back(false);
        return *this;
    }

    JsonWriter& JsonWriter::endArray() {
        const bool items = hasItems_.back();
        hasItems_.pop_back();
        if (items) {
            indent();
        }
        out_.push_back(']');
        return *this;
    }

    JsonWriter& JsonWriter::key(std::string_view name) {
        beforeValue();
        out_ += jsonQuote(name);
        out_ += ": ";
        afterKey_ = true;
        return *this;
    }

    JsonWriter& JsonWriter::value(std::string_view text) {
        beforeValue();
        out_ += jsonQuote(text);
        return *this;
    }

    JsonWriter& JsonWriter::value(std::uint64_t number) {
        beforeValue();
        out_ += std::to_string(number);
        return *this;
    }

    JsonWriter& JsonWriter::value(std::int64_t number) {
        beforeValue();
        out_ += std::to_string(number);
        return *this;
    }

    JsonWriter& JsonWriter::value(double number) {
        if (!std::isfinite(number)) {
            return null();
        }
        beforeValue();
        out_ += std::format("{:.6g}", number);
        return *this;
    }

    JsonWriter& JsonWriter::value(bool flag) {
        beforeValue();
        out_ += flag ? "true" : "false";
        return *this;
    }

    JsonWriter& JsonWriter::null() {
        beforeValue();
        out_ += "null";
        return *this;
    }

} // namespace icd::core
