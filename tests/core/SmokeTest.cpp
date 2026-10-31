#include "core/format/ByteSize.h"
#include "core/format/Count.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace icd::core { namespace {

    constexpr std::uint64_t KB = 1024;
    constexpr std::uint64_t TB = KB * KB * KB * KB;

    // Small counts are exact and use the singular for one byte.
    TEST(FormatBytes, BytesBelowOneKilobyte) {
        EXPECT_EQ(formatBytes(0), "0 bytes");
        EXPECT_EQ(formatBytes(1), "1 byte");
        EXPECT_EQ(formatBytes(1023), "1023 bytes");
    }

    // Three significant digits, like Explorer.
    TEST(FormatBytes, ThreeSignificantDigits) {
        EXPECT_EQ(formatBytes(KB), "1.00 KB");
        EXPECT_EQ(formatBytes(1536), "1.50 KB");
        EXPECT_EQ(formatBytes(10 * KB), "10.0 KB");
        EXPECT_EQ(formatBytes(512 * KB), "512 KB");
    }

    // A value that would round to 1000+ of a unit moves to the next unit instead.
    TEST(FormatBytes, RoundsUpIntoNextUnit) { EXPECT_EQ(formatBytes(1023 * KB), "1.00 MB"); }

    // The design target (30 TB drives) and the largest possible value both format.
    TEST(FormatBytes, LargeVolumes) {
        EXPECT_EQ(formatBytes(30 * TB), "30.0 TB");
        EXPECT_EQ(formatBytes(std::numeric_limits<std::uint64_t>::max()), "16.0 EB");
    }

    // Groups of three digits, separated by commas, at every length.
    TEST(FormatCount, ThousandsSeparators) {
        EXPECT_EQ(formatCount(0), "0");
        EXPECT_EQ(formatCount(999), "999");
        EXPECT_EQ(formatCount(1000), "1,000");
        EXPECT_EQ(formatCount(10000), "10,000");
        EXPECT_EQ(formatCount(123456), "123,456");
        EXPECT_EQ(formatCount(1234567), "1,234,567");
        EXPECT_EQ(formatCount(std::numeric_limits<std::uint64_t>::max()), "18,446,744,073,709,551,615");
    }

}} // namespace icd::core
