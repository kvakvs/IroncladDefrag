#include "platform/win/LogSinks.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace icd::platform { namespace {

    namespace fs = std::filesystem;

    // A fresh, empty directory under %TEMP% per test, removed again afterwards.
    class LogSinksTest : public ::testing::Test {
    protected:
        // Creates a unique directory for this test.
        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            dir = fs::temp_directory_path() / "icd_tests" /
                (std::string(info->name()) + "_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(dir);
        }

        // Removes the directory and everything the test wrote.
        void TearDown() override {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }

        // Whole file content, for comparing what the sink wrote.
        static std::string read(const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            return text.str();
        }

        fs::path dir; // Test-owned directory.
    };

    // Lines end with CRLF and land in <dir>/<base>.log.
    TEST_F(LogSinksTest, WritesLinesToFile) {
        FileSink sink({dir, "app"});
        sink.write({}, "one");
        sink.write({}, "two");
        EXPECT_EQ(sink.path(), dir / "app.log");
        EXPECT_EQ(read(dir / "app.log"), "one\r\ntwo\r\n");
    }

    // A missing directory is created.
    TEST_F(LogSinksTest, CreatesDirectory) {
        FileSink sink({dir / "a" / "b", "app"});
        sink.write({}, "x");
        EXPECT_TRUE(fs::exists(dir / "a" / "b" / "app.log"));
    }

    // A full file moves to .1.log, older ones shift up, and only keepFiles old files remain.
    TEST_F(LogSinksTest, RotatesAndKeepsLimitedHistory) {
        FileSink sink({dir, "app", 10, 2}); // 10 bytes: every 8-byte line ("line N" + CRLF) fills a file.
        for (int i = 1; i <= 4; ++i) {
            sink.write({}, "line " + std::to_string(i));
        }
        EXPECT_EQ(read(dir / "app.log"), "line 4\r\n");
        EXPECT_EQ(read(dir / "app.1.log"), "line 3\r\n");
        EXPECT_EQ(read(dir / "app.2.log"), "line 2\r\n");
        EXPECT_FALSE(fs::exists(dir / "app.3.log"));
    }

    // Reopening appends to an existing small file and rotates an existing full one.
    TEST_F(LogSinksTest, ReopenAppendsOrRotates) {
        {
            FileSink sink({dir, "app", 100, 2});
            sink.write({}, "first");
        }
        {
            FileSink sink({dir, "app", 100, 2});
            sink.write({}, "second");
        }
        EXPECT_EQ(read(dir / "app.log"), "first\r\nsecond\r\n");
        {
            FileSink sink({dir, "app", 10, 2}); // Existing 15 bytes >= 10: rotate on open.
            sink.write({}, "third");
        }
        EXPECT_EQ(read(dir / "app.1.log"), "first\r\nsecond\r\n");
        EXPECT_EQ(read(dir / "app.log"), "third\r\n");
    }

    // The default location is the per-user LocalAppData folder.
    TEST(LogDirectory, IsUnderLocalAppData) {
        const fs::path path = defaultLogDirectory();
        EXPECT_EQ(path.filename(), "logs");
        EXPECT_EQ(path.parent_path().filename(), "IroncladDefrag");
    }

}} // namespace icd::platform
