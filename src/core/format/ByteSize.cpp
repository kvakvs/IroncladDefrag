#include "core/format/ByteSize.h"

#include <array>
#include <cstdio>

namespace icd::core {

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
