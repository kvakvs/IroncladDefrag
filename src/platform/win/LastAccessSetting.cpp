#include "platform/win/LastAccessSetting.h"

#include <windows.h>

namespace icd::platform {

    namespace {

        // Bit set when the value carries the management mode (Windows 10 1803 and later).
        constexpr std::uint32_t managedFlag = 0x8000'0000U;

        // Bit set when Windows manages the setting itself.
        constexpr std::uint32_t systemManagedBit = 0x2U;

        // Bit set when updates are off.
        constexpr std::uint32_t disabledBit = 0x1U;

    } // namespace

    LastAccessSetting decodeLastAccessSetting(std::optional<std::uint32_t> raw) {
        LastAccessSetting s;
        s.raw = raw;
        if (!raw) {
            return s;
        }
        const bool modern = (*raw & managedFlag) != 0 && (*raw & ~managedFlag) <= 3;
        const bool legacy = *raw <= 1;
        if (modern || legacy) {
            s.systemManaged = modern && (*raw & systemManagedBit) != 0;
            s.updatesEnabled = (*raw & disabledBit) == 0;
        }
        return s;
    }

    LastAccessSetting readLastAccessSetting() {
        DWORD value = 0;
        DWORD size = sizeof(value);
        const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                                            L"NtfsDisableLastAccessUpdate", RRF_RT_REG_DWORD, nullptr, &value, &size);
        return decodeLastAccessSetting(status == ERROR_SUCCESS ? std::optional<std::uint32_t>(value) : std::nullopt);
    }

} // namespace icd::platform
