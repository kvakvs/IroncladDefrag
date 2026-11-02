#include "core/format/ByteSize.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <limits>
#include <utility>

namespace icd::core {

    namespace {

        // Binary units of size specs, largest first so sizeSpecText() picks the largest exact one.
        constexpr std::array<std::pair<std::string_view, std::uint64_t>, 5> specUnits{{
            {"TB", 1ULL << 40U},
            {"GB", 1ULL << 30U},
            {"MB", 1ULL << 20U},
            {"KB", 1ULL << 10U},
            {"B", 1},
        }};

    } // namespace

    std::optional<std::uint64_t> parseSizeSpec(std::string_view text) {
        std::uint64_t number = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (error != std::errc()) {
            return std::nullopt;
        }
        std::string_view unit(end, text.data() + text.size());
        unit.remove_prefix(std::min(unit.find_first_not_of(' '), unit.size()));
        if (unit.empty()) {
            return number;
        }
        for (const auto& [name, factor] : specUnits) {
            if (name == unit) {
                const bool fits = number <= std::numeric_limits<std::uint64_t>::max() / factor;
                return fits ? std::optional(number * factor) : std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::string sizeSpecText(std::uint64_t bytes) {
        for (const auto& [name, factor] : specUnits) {
            if (bytes != 0 && bytes % factor == 0) {
                return std::to_string(bytes / factor) + " " + std::string(name);
            }
        }
        return "0 B";
    }

    std::string formatBytes(std::uint64_t bytes) {
        if (bytes < 1024) {
            return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
        }
        static constexpr std::array<const char*, 6> units{"KB", "MB", "GB", "TB", "PB", "EB"};
        double value = static_cast<double>(bytes) / 1024.0;
        std::size_t unit = 0;
        // Move up a unit before rounding would print "1024" or "1.00e3" of the smaller unit.
        while (value >= 999.5 && unit + 1 < units.size()) {
            value /= 1024.0;
            ++unit;
        }
        const char* format = value < 9.995 ? "%.2f %s" : value < 99.95 ? "%.1f %s" : "%.0f %s";
        std::array<char, 32> buffer{};
        std::snprintf(buffer.data(), buffer.size(), format, value, units[unit]);
        return buffer.data();
    }

} // namespace icd::core
