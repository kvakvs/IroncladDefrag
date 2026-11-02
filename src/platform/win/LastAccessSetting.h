#pragma once

#include <cstdint>
#include <optional>

namespace icd::platform {

    // NTFS's last-access behaviour (the `disablelastaccess` setting, plan.md 8.2). It applies to every NTFS volume of
    // the system; access-based strategies can only trust access times while updates are on.
    struct LastAccessSetting {
        std::optional<std::uint32_t> raw; // NtfsDisableLastAccessUpdate as stored; empty when absent or unreadable.
        bool systemManaged = false; // Windows decides at boot (on when the system volume is 128 GB or less).
        std::optional<bool> updatesEnabled; // Whether NTFS updates access times now; empty when unknown.
    };

    // What a raw NtfsDisableLastAccessUpdate value means: 0x80000000-0x80000003 (user or system managed, on or off),
    // the older 0 (on) and 1 (off); anything else is unknown. Pure, so the decoding is tested.
    [[nodiscard]] LastAccessSetting decodeLastAccessSetting(std::optional<std::uint32_t> raw);

    // Reads the setting from HKLM\SYSTEM\CurrentControlSet\Control\FileSystem (no administrator rights needed).
    [[nodiscard]] LastAccessSetting readLastAccessSetting();

} // namespace icd::platform
