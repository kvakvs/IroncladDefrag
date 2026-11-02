#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace icd::core {

    // Formats a byte count for people, Explorer style (1024-based, three significant digits): "512 bytes",
    // "1.50 KB", "12.3 GB", "27.3 TB".
    std::string formatBytes(std::uint64_t bytes);

    // Reads a size as data files write it: plain bytes ("4096") or a whole number with a binary unit ("64 MB",
    // "2 GB"; B, KB, MB, GB, TB), so presets stay readable. nullopt for anything else or an overflow.
    [[nodiscard]] std::optional<std::uint64_t> parseSizeSpec(std::string_view text);

    // Writes a size the way parseSizeSpec() reads it, in the largest unit that holds it exactly ("64 MB", "1000 B"),
    // so written files round-trip and stay readable.
    [[nodiscard]] std::string sizeSpecText(std::uint64_t bytes);

} // namespace icd::core
