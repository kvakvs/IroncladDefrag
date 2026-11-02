// Shadow copy detection (T090): counting a volume's copies in a vssadmin listing, and on the `vss` test scenario.

#include "platform/win/ShadowCopies.h"
#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

    // A listing as vssadmin prints it (trimmed), with two copies of one volume and one of another.
    constexpr std::string_view listing = R"(vssadmin 1.1 - Volume Shadow Copy Service administrative command-line tool
Contents of shadow copy set ID: {1}
   Contained 1 shadow copies at creation time: 10/31/2026 9:00:00 AM
      Shadow Copy ID: {a}
         Original Volume: (T:)\?\Volume{0aa1b2c3-0000-0000-0000-000000000001}\
         Shadow Copy Volume: \?\GLOBALROOT\Device\HarddiskVolumeShadowCopy1
Contents of shadow copy set ID: {2}
      Shadow Copy ID: {b}
         Original Volume: (T:)\?\VOLUME{0AA1B2C3-0000-0000-0000-000000000001}\
Contents of shadow copy set ID: {3}
      Shadow Copy ID: {c}
         Original Volume: (C:)\?\Volume{ffff0000-0000-0000-0000-000000000002}\
)";

    TEST(ShadowCopies, CountsTheVolumesCopiesInAListing) {
        EXPECT_EQ(icd::platform::countShadowsInListing({.text = listing},
                                                       R"(\?\Volume{0aa1b2c3-0000-0000-0000-000000000001}\)"),
                  2U);
        EXPECT_EQ(icd::platform::countShadowsInListing({.text = listing},
                                                       R"(\?\Volume{ffff0000-0000-0000-0000-000000000002}\)"),
                  1U);
        EXPECT_EQ(icd::platform::countShadowsInListing({.text = listing},
                                                       R"(\?\Volume{12345678-0000-0000-0000-000000000003}\)"),
                  0U);
        EXPECT_EQ(icd::platform::countShadowsInListing({.text = "No items found that satisfy the query."}, ""), 0U);
    }

    TEST(ShadowCopies, DetectsTheVssScenario) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        int checked = 0;
        for (const auto& v : icd::platform::enumerateVolumes()) {
            if (v.fileSystem != L"NTFS" || !v.label.starts_with(L"ICD-TEST") || v.mountPaths.empty()) {
                continue;
            }
            const auto count = icd::platform::countShadowCopies(v.volumeName);
            ASSERT_TRUE(count.has_value());
            if (std::filesystem::exists(std::filesystem::path(v.mountPaths.front()) / L"vss" / L"after")) {
                EXPECT_GE(*count, 1U) << icd::platform::toUtf8(v.mountPaths.front());
                ++checked;
            }
        }
        if (checked == 0) {
            GTEST_SKIP() << "no ICD-TEST volume with the vss scenario mounted";
        }
    }

} // namespace
