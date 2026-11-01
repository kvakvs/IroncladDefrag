#include "core/ntfs/NtfsText.h"

#include <cstdint>

namespace icd::core::ntfs {

    namespace {

        constexpr char32_t replacement = 0xFFFD;

        // Appends one code point as UTF-8.
        void appendUtf8(std::string& out, char32_t c) {
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            }
            else if (c < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (c >> 6U)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
            }
            else if (c < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (c >> 12U)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6U) & 0x3FU)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
            }
            else {
                out.push_back(static_cast<char>(0xF0 | (c >> 18U)));
                out.push_back(static_cast<char>(0x80 | ((c >> 12U) & 0x3FU)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6U) & 0x3FU)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3FU)));
            }
        }

        // Code unit i of the text.
        char16_t unitAt(std::span<const std::byte> text, std::size_t i) {
            return static_cast<char16_t>(static_cast<std::uint8_t>(text[2 * i]) |
                                         (static_cast<std::uint8_t>(text[2 * i + 1]) << 8U));
        }

        // Whether a code unit is a high (leading) or low (trailing) surrogate.
        constexpr bool isHigh(char16_t u) { return u >= 0xD800 && u <= 0xDBFF; }
        constexpr bool isLow(char16_t u) { return u >= 0xDC00 && u <= 0xDFFF; }

    } // namespace

    std::string utf16ToUtf8(std::span<const std::byte> utf16le) {
        const std::size_t units = utf16le.size() / 2;
        std::string out;
        out.reserve(units);
        for (std::size_t i = 0; i < units; ++i) {
            const char16_t u = unitAt(utf16le, i);
            const bool pair = isHigh(u) && i + 1 < units && isLow(unitAt(utf16le, i + 1));
            if (pair) {
                const char16_t low = unitAt(utf16le, ++i);
                appendUtf8(out, 0x10000 + ((char32_t{u} - 0xD800) << 10U) + (char32_t{low} - 0xDC00));
            }
            else {
                appendUtf8(out, isHigh(u) || isLow(u) ? replacement : char32_t{u});
            }
        }
        return out;
    }

} // namespace icd::core::ntfs
