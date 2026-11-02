// Last-access setting (T072): decoding of every NtfsDisableLastAccessUpdate value, and the registry reading equals
// what `fsutil behavior query disablelastaccess` reports on this machine.

#include "platform/win/ChildProcess.h"
#include "platform/win/LastAccessSetting.h"

#include <gtest/gtest.h>

#include <string>

namespace icd::platform { namespace {

    TEST(LastAccessSetting, DecodesEveryValue) {
        const auto on = [](std::uint32_t raw) { return decodeLastAccessSetting(raw).updatesEnabled; };
        EXPECT_EQ(on(0x8000'0000U), true); // User managed, on.
        EXPECT_EQ(on(0x8000'0001U), false); // User managed, off.
        EXPECT_EQ(on(0x8000'0002U), true); // System managed, on.
        EXPECT_EQ(on(0x8000'0003U), false); // System managed, off.
        EXPECT_EQ(on(0), true); // Before Windows 10 1803.
        EXPECT_EQ(on(1), false);
        EXPECT_FALSE(on(7).has_value()); // Unknown values stay unknown.
        EXPECT_FALSE(on(0x8000'0004U).has_value());
        EXPECT_FALSE(decodeLastAccessSetting(std::nullopt).updatesEnabled.has_value());
        EXPECT_TRUE(decodeLastAccessSetting(0x8000'0003U).systemManaged);
        EXPECT_FALSE(decodeLastAccessSetting(0x8000'0001U).systemManaged);
        EXPECT_FALSE(decodeLastAccessSetting(1).systemManaged);
    }

    TEST(LastAccessSetting, MatchesFsutil) {
        std::string output;
        const auto code = runCaptured(
            L"\"" + systemProgram(L"fsutil.exe") + L"\" behavior query disablelastaccess",
            [&](std::string_view chunk) { output += chunk; }, [] { return false; });
        ASSERT_EQ(code, 0U) << output;
        // "DisableLastAccess = 2  (System Managed, Last Access Time Updates ENABLED)"
        const std::size_t equals = output.find('=');
        ASSERT_NE(equals, std::string::npos) << output;
        const auto reported = static_cast<std::uint32_t>(std::stoul(output.substr(equals + 1)));
        const LastAccessSetting s = readLastAccessSetting();
        ASSERT_TRUE(s.raw.has_value());
        EXPECT_EQ(*s.raw & 0x7FFF'FFFFU, reported);
        EXPECT_EQ(s.updatesEnabled, output.find("ENABLED") != std::string::npos) << output;
    }

}} // namespace icd::platform
