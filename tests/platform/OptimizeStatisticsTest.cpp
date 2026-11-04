// Windows optimizer statistics (T123): the key name of a volume, LastRunTime decoding (a local-time SYSTEMTIME, as
// Optimize Drives writes it) and reads of volumes the optimizer never saw.

#include "platform/win/OptimizeStatistics.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace icd::platform { namespace {

    // The bytes of a local SYSTEMTIME as the registry stores them.
    std::array<std::uint8_t, sizeof(SYSTEMTIME)> bytesOf(const SYSTEMTIME& local) {
        std::array<std::uint8_t, sizeof(SYSTEMTIME)> bytes{};
        std::memcpy(bytes.data(), &local, sizeof(local));
        return bytes;
    }

    TEST(OptimizeStatistics, KeyNameFromVolumeGuidPath) {
        EXPECT_EQ(statisticsKeyName(L"\\\\?\\Volume{8595bbee-ba3a-43f5-974e-efabbe1d4b55}\\"),
                  L"Volume{8595bbee-ba3a-43f5-974e-efabbe1d4b55}");
        EXPECT_EQ(statisticsKeyName(L"Volume{8595bbee-ba3a-43f5-974e-efabbe1d4b55}"),
                  L"Volume{8595bbee-ba3a-43f5-974e-efabbe1d4b55}");
        EXPECT_EQ(statisticsKeyName(L"C:\\"), L"");
        EXPECT_EQ(statisticsKeyName(L""), L"");
        EXPECT_EQ(statisticsKeyName(L"\\\\?\\Volume{\\"), L"");
    }

    TEST(OptimizeStatistics, NeverRunAndBadValuesHaveNoTime) {
        EXPECT_FALSE(decodeLastRunTime(std::array<std::uint8_t, 16>{}));
        EXPECT_FALSE(decodeLastRunTime(std::array<std::uint8_t, 8>{0xEA, 0x07, 0x0B, 0x00}));
        SYSTEMTIME bad{.wYear = 2026,
                       .wMonth = 13,
                       .wDayOfWeek = 0,
                       .wDay = 4,
                       .wHour = 0,
                       .wMinute = 0,
                       .wSecond = 0,
                       .wMilliseconds = 0};
        EXPECT_FALSE(decodeLastRunTime(bytesOf(bad)));
    }

    // A time written in local time decodes to the same instant: UTC -> local SYSTEMTIME -> decode.
    TEST(OptimizeStatistics, LastRunTimeIsLocalTime) {
        const SYSTEMTIME utc{.wYear = 2026,
                             .wMonth = 11,
                             .wDayOfWeek = 3,
                             .wDay = 4,
                             .wHour = 2,
                             .wMinute = 10,
                             .wSecond = 51,
                             .wMilliseconds = 117};
        SYSTEMTIME local{};
        ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
        const auto decoded = decodeLastRunTime(bytesOf(local));
        ASSERT_TRUE(decoded);
        using namespace std::chrono;
        const auto expected = sys_days(year(2026) / November / 4) + 2h + 10min + 51s + 117ms;
        EXPECT_EQ(time_point_cast<milliseconds>(*decoded), expected);
    }

    TEST(OptimizeStatistics, UnknownVolumesHaveNoStatistics) {
        EXPECT_FALSE(readOptimizeStatistics(L"C:\\"));
        EXPECT_FALSE(readOptimizeStatistics(L"\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\"));
    }

}} // namespace icd::platform
