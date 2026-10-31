#include "core/log/Log.h"

#include <array>

namespace icd::core::logging {

    namespace {

        // Appends text with control characters, quotes and backslashes escaped, so it stays on one line.
        void appendEscaped(std::string& out, std::string_view text) {
            for (const char c : text) {
                switch (c) {
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '"':
                case '\\':
                    out += '\\';
                    out += c;
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        out += std::format("\\x{:02x}", static_cast<unsigned char>(c));
                    }
                    else {
                        out += c;
                    }
                }
            }
        }

        // A value needs quotes when it is empty or would be misread as several fields.
        bool needsQuotes(std::string_view value) {
            if (value.empty()) {
                return true;
            }
            for (const char c : value) {
                // Backslashes alone do not force quotes, so plain Windows paths stay readable.
                if (c == ' ' || c == '"' || c == '=' || static_cast<unsigned char>(c) < 0x20) {
                    return true;
                }
            }
            return false;
        }

        // Appends " key=value", quoting the value when needed.
        void appendField(std::string& out, const Field& field) {
            out += ' ';
            out += field.key;
            out += '=';
            if (!needsQuotes(field.value)) {
                out += field.value;
                return;
            }
            out += '"';
            appendEscaped(out, field.value);
            out += '"';
        }

    } // namespace

    std::string_view levelName(Level level) {
        static constexpr std::array<std::string_view, 5> names{"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR"};
        const auto index = static_cast<std::size_t>(level);
        return index < names.size() ? names[index] : "?????";
    }

    std::string formatRecord(const Record& record) {
        const auto millis = std::chrono::floor<std::chrono::milliseconds>(record.time);
        std::string out = std::format("{:%FT%T}Z {} t={} ", millis, levelName(record.level), record.thread);
        appendEscaped(out, record.message);
        for (const Field& field : record.fields) {
            appendField(out, field);
        }
        return out;
    }

} // namespace icd::core::logging
