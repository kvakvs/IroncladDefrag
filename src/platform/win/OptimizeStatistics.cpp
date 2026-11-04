#include "platform/win/OptimizeStatistics.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>

namespace icd::platform {

    namespace {

        // Where the optimizer keeps one key per volume.
        constexpr std::wstring_view statisticsRoot = L"SOFTWARE\\Microsoft\\Dfrg\\Statistics\\";

        // FILETIME ticks (100 ns since 1601) to the system clock.
        std::chrono::system_clock::time_point fromFileTime(const FILETIME& time) {
            constexpr std::int64_t unixEpochTicks = 116'444'736'000'000'000; // 1970-01-01 in FILETIME ticks.
            const std::int64_t ticks =
                (static_cast<std::int64_t>(time.dwHighDateTime) << 32U) | static_cast<std::int64_t>(time.dwLowDateTime);
            return std::chrono::system_clock::time_point(
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>(ticks - unixEpochTicks)));
        }

        // A REG_DWORD or REG_QWORD value of key; nullopt when absent or of another type.
        std::optional<std::uint64_t> readUnsigned(const std::wstring& key, const wchar_t* name) {
            std::uint64_t value = 0;
            DWORD size = sizeof(value);
            DWORD type = 0;
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), name, RRF_RT_REG_DWORD | RRF_RT_REG_QWORD, &type, &value,
                             &size) != ERROR_SUCCESS) {
                return std::nullopt;
            }
            return type == REG_DWORD ? value & 0xFFFF'FFFFULL : value;
        }

    } // namespace

    std::wstring statisticsKeyName(std::wstring_view volumeName) {
        constexpr std::wstring_view prefix = L"\\\\?\\";
        if (volumeName.starts_with(prefix)) {
            volumeName.remove_prefix(prefix.size());
        }
        if (volumeName.ends_with(L'\\')) {
            volumeName.remove_suffix(1);
        }
        constexpr std::wstring_view head = L"volume{";
        const auto sameLetter = [](wchar_t a, wchar_t b) { return std::towlower(a) == b; };
        const bool guid = volumeName.size() > head.size() + 1 &&
            std::ranges::equal(volumeName.substr(0, head.size()), head, sameLetter) && volumeName.back() == L'}';
        return guid ? std::wstring(volumeName) : std::wstring();
    }

    std::optional<std::chrono::system_clock::time_point> decodeLastRunTime(std::span<const std::uint8_t> bytes) {
        SYSTEMTIME local{};
        if (bytes.size() != sizeof(local) || std::ranges::all_of(bytes, [](std::uint8_t b) { return b == 0; })) {
            return std::nullopt;
        }
        std::memcpy(&local, bytes.data(), sizeof(local));
        SYSTEMTIME utc{};
        FILETIME time{};
        if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &time)) {
            return std::nullopt; // Not a valid date.
        }
        return fromFileTime(time);
    }

    std::optional<OptimizeStatistics> readOptimizeStatistics(std::wstring_view volumeName) {
        const std::wstring name = statisticsKeyName(volumeName);
        if (name.empty()) {
            return std::nullopt;
        }
        const std::wstring key = std::wstring(statisticsRoot) + name;
        std::array<std::uint8_t, sizeof(SYSTEMTIME)> raw{};
        DWORD size = static_cast<DWORD>(raw.size());
        const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"LastRunTime", RRF_RT_REG_BINARY, nullptr,
                                            raw.data(), &size);
        if (status != ERROR_SUCCESS) {
            return std::nullopt; // No key: the optimizer never ran on this volume.
        }
        OptimizeStatistics statistics;
        statistics.lastRun = decodeLastRunTime(std::span(raw).first(size));
        statistics.clustersTrimmed = readUnsigned(key, L"LastRunClustersTrimmed").value_or(0);
        statistics.bytesPerCluster = static_cast<std::uint32_t>(readUnsigned(key, L"BytesPerCluster").value_or(0));
        return statistics;
    }

} // namespace icd::platform
