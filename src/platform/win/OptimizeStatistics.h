#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace icd::platform {

    // What Windows' storage optimizer (defrag.exe, Optimize Drives, the scheduled task) recorded about its last run on
    // a volume, kept in HKLM\SOFTWARE\Microsoft\Dfrg\Statistics\Volume{GUID}. Reused so the SSD rows show the same
    // "Last run" as Optimize Drives and a TRIM can report how much it trimmed (plan.md 10.5).
    struct OptimizeStatistics {
        std::optional<std::chrono::system_clock::time_point> lastRun; // LastRunTime; empty when never run.
        std::uint64_t clustersTrimmed = 0; // LastRunClustersTrimmed: clusters the last retrim sent TRIM for.
        std::uint32_t bytesPerCluster = 0; // BytesPerCluster as recorded; 0 when the optimizer never filled it.

        // Bytes the last retrim trimmed; 0 when unknown.
        [[nodiscard]] std::uint64_t bytesTrimmed() const { return clustersTrimmed * bytesPerCluster; }
    };

    // The statistics key name of a volume GUID path: "\\?\Volume{...}\" -> "Volume{...}"; empty for other names.
    [[nodiscard]] std::wstring statisticsKeyName(std::wstring_view volumeName);

    // LastRunTime as stored: a 16-byte SYSTEMTIME in local time. Empty when all zero (never run), of another size or
    // not a valid date. Pure apart from the time zone rules, so the decoding is tested.
    [[nodiscard]] std::optional<std::chrono::system_clock::time_point>
    decodeLastRunTime(std::span<const std::uint8_t> bytes);

    // Reads a volume's statistics (no administrator rights needed); empty when the optimizer never saw the volume or
    // the volume name is not a GUID path.
    [[nodiscard]] std::optional<OptimizeStatistics> readOptimizeStatistics(std::wstring_view volumeName);

} // namespace icd::platform
