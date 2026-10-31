#pragma once

#include <cstdint>
#include <string>

namespace icd::core {

    // Formats a byte count for people, Explorer style (1024-based, three significant digits): "512 bytes",
    // "1.50 KB", "12.3 GB", "27.3 TB".
    std::string formatBytes(std::uint64_t bytes);

} // namespace icd::core
