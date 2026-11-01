// Child process capture (T044): output arrives through the pipe, the exit code is returned, and a cancelled
// run terminates the child quickly.

#include "platform/win/ChildProcess.h"

#include <gtest/gtest.h>

#include <chrono>

namespace {

    using icd::platform::runCaptured;
    using icd::platform::systemProgram;

    TEST(ChildProcess, CapturesOutputAndExitCode) {
        std::string output;
        const auto code = runCaptured(
            L"\"" + systemProgram(L"cmd.exe") + L"\" /c echo hello& echo err 1>&2& exit 3",
            [&](std::string_view chunk) { output += chunk; }, [] { return false; });
        EXPECT_EQ(code, 3U);
        EXPECT_NE(output.find("hello"), std::string::npos);
        EXPECT_NE(output.find("err"), std::string::npos);
    }

    TEST(ChildProcess, CancelTerminatesTheChild) {
        const auto start = std::chrono::steady_clock::now();
        int polls = 0;
        const auto code = runCaptured(
            L"\"" + systemProgram(L"PING.EXE") + L"\" -n 30 127.0.0.1", [](std::string_view) {},
            [&] { return ++polls > 4; });
        EXPECT_EQ(code, 1U);
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    }

    TEST(ChildProcess, ReportsProgramsThatCannotStart) {
        EXPECT_THROW(
            runCaptured(L"Z:\\does\\not\\exist.exe", [](std::string_view) {}, [] { return false; }), std::runtime_error);
    }

} // namespace
