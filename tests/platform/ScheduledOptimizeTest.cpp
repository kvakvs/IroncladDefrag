// Windows' scheduled optimization (T120): the drive choice in the ScheduledDefrag task's arguments, read and written in
// the forms the Optimize Drives dialog itself writes (recorded from it on Windows 11), and the live task read back.

#include "platform/win/ScheduledOptimize.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace icd::platform { namespace {

    const std::wstring volA = L"\\\\?\\Volume{b771788f-4c29-4270-aefa-9512bdf84e76}\\";
    const std::wstring volB = L"\\\\?\\Volume{d51ea3f5-4c63-4bff-a9df-26193caba017}\\";

    // The three forms the dialog writes parse to their modes and format back to the same text.
    TEST(ScheduledOptimize, ReadsAndWritesTheDialogsForms) {
        const std::vector<std::pair<std::wstring, ScheduleSelection>> forms{
            {L"-c -h -o -$", {.mode = ScheduleMode::All, .volumes = {}, .flags = L"-h -o -$"}},
            {volA + L" " + volB + L" -e -h -o -$",
             {.mode = ScheduleMode::AllExcept, .volumes = {volA, volB}, .flags = L"-h -o -$"}},
            {volA + L" -h -o -$", {.mode = ScheduleMode::Only, .volumes = {volA}, .flags = L"-h -o -$"}},
        };
        for (const auto& [text, selection] : forms) {
            EXPECT_EQ(parseScheduleArguments(text), selection);
            EXPECT_EQ(scheduleArguments(selection), text);
        }
        // Other spellings: slashes, upper case, extra spaces; -c wins over a list; other flags are kept in order.
        EXPECT_EQ(parseScheduleArguments(L"  /C  D: -k -g -$ ").mode, ScheduleMode::All);
        EXPECT_EQ(parseScheduleArguments(L"/E D: -k -g -$"),
                  (ScheduleSelection{.mode = ScheduleMode::AllExcept, .volumes = {L"D:"}, .flags = L"-k -g -$"}));
    }

    // A volume is matched by its GUID path or a mount path, in any case and with or without the backslash.
    TEST(ScheduledOptimize, CoversByAnyName) {
        const std::vector<std::wstring> a{volA, L"T:\\"};
        const std::vector<std::wstring> b{volB, L"Q:\\"};
        const ScheduleSelection except{.mode = ScheduleMode::AllExcept, .volumes = {volA}};
        EXPECT_FALSE(scheduleCovers(except, a));
        EXPECT_TRUE(scheduleCovers(except, b));
        const ScheduleSelection byLetter{.mode = ScheduleMode::Only, .volumes = {L"t:"}};
        EXPECT_TRUE(scheduleCovers(byLetter, a));
        EXPECT_FALSE(scheduleCovers(byLetter, b));
        std::wstring upper = volA;
        for (wchar_t& c : upper) {
            c = static_cast<wchar_t>(std::towupper(c));
        }
        EXPECT_TRUE(scheduleCovers({.mode = ScheduleMode::Only, .volumes = {upper}}, a));
        EXPECT_TRUE(scheduleCovers({}, a)); // All.
    }

    // Opt-out and restore change as little as possible and end where the dialog would: back to "-c" once nothing is
    // left out, and never an Only list without drives.
    TEST(ScheduledOptimize, OptOutAndRestore) {
        const std::vector<std::wstring> a{volA, L"T:\\"};
        const std::vector<std::wstring> b{volB, L"Q:\\"};
        const ScheduleSelection all;
        const auto outA = withScheduled(all, a, false);
        ASSERT_TRUE(outA);
        EXPECT_EQ(scheduleArguments(*outA), volA + L" -e -h -o -$");
        const auto outAB = withScheduled(*outA, b, false);
        ASSERT_TRUE(outAB);
        EXPECT_EQ(scheduleArguments(*outAB), volA + L" " + volB + L" -e -h -o -$");
        EXPECT_EQ(withScheduled(*outAB, b, false), outAB); // Already out.
        const auto backA = withScheduled(*outAB, a, true);
        ASSERT_TRUE(backA);
        EXPECT_EQ(scheduleArguments(*backA), volB + L" -e -h -o -$");
        const auto backAll = withScheduled(*backA, b, true);
        ASSERT_TRUE(backAll);
        EXPECT_EQ(*backAll, all);
        EXPECT_EQ(withScheduled(all, a, true), all); // Already on.

        const ScheduleSelection only{.mode = ScheduleMode::Only, .volumes = {volA}};
        const auto withB = withScheduled(only, b, true);
        ASSERT_TRUE(withB);
        EXPECT_EQ(scheduleArguments(*withB), volA + L" " + volB + L" -h -o -$");
        const auto withoutA = withScheduled(*withB, a, false);
        ASSERT_TRUE(withoutA);
        EXPECT_EQ(scheduleArguments(*withoutA), volB + L" -h -o -$");
        EXPECT_FALSE(withScheduled(*withoutA, b, false)); // The last drive of the schedule.
        EXPECT_EQ(withScheduled(only, {}, false), only); // No names: nothing to do.
    }

    // Runs a PowerShell command and returns its first output line.
    std::string powershellLine(const char* command) {
        const std::string line = std::string("powershell -NoProfile -Command \"") + command + "\"";
        FILE* pipe = _popen(line.c_str(), "rt");
        if (pipe == nullptr) {
            return {};
        }
        char buffer[2048] = {};
        std::string text = std::fgets(buffer, sizeof(buffer), pipe) != nullptr ? buffer : "";
        _pclose(pipe);
        text.erase(text.find_last_not_of("\r\n ") + 1);
        return text;
    }

    // The live task reads as Windows reports it (administrator rights not needed to read).
    TEST(ScheduledOptimize, ReadsTheWindowsTask) {
        const auto state = readSchedule();
        ASSERT_TRUE(state.has_value());
        const std::string expected = powershellLine("$t = Get-ScheduledTask -TaskName ScheduledDefrag; "
                                                    "'{0}|{1}' -f ($t.State -ne 'Disabled'), $t.Actions[0].Arguments");
        const std::wstring arguments = scheduleArguments(state->selection);
        const std::string got =
            std::string(state->taskEnabled ? "True" : "False") + "|" + std::string(arguments.begin(), arguments.end());
        EXPECT_EQ(got, expected);
    }

}} // namespace icd::platform
