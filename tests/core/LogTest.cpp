#include "core/log/Log.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace icd::core::logging { namespace {

    // Collects lines in memory so tests can inspect exactly what a logger wrote.
    class MemorySink : public Sink {
    public:
        void write(const Record& /*record*/, std::string_view line) override {
            const std::scoped_lock lock(mutex);
            lines.emplace_back(line);
        }

        std::mutex mutex; // Sinks are called under the logger lock; this guards test reads.
        std::vector<std::string> lines; // Every line written, in order.
    };

    // A record at a fixed time and without a thread id, so the formatted text is predictable.
    Record fixedRecord(Level level, std::string message, std::vector<Field> fields = {}) {
        using namespace std::chrono;
        const sys_days day = year{2026} / October / 31;
        return Record{day + hours{19} + minutes{10} + seconds{5} + milliseconds{123}, level, std::thread::id{},
                      std::move(message), std::move(fields)};
    }

    // The line layout: UTC time with milliseconds, padded level, thread, message, fields.
    TEST(LogFormat, LineLayout) {
        const std::string line =
            formatRecord(fixedRecord(Level::Info, "startup", {{"app", "IroncladDefrag"}, {"files", 42}, {"ok", true}}));
        const std::string thread = std::format("{}", std::thread::id{});
        EXPECT_EQ(line, "2026-10-31T19:10:05.123Z INFO  t=" + thread + " startup app=IroncladDefrag files=42 ok=true");
    }

    // Values that would break key=value parsing are quoted and escaped.
    TEST(LogFormat, QuotesAndEscapesValues) {
        const std::string line = formatRecord(
            fixedRecord(Level::Warning, "x", {{"path", "C:\\a b"}, {"empty", ""}, {"q", "say \"hi\""}, {"eq", "a=b"}}));
        EXPECT_NE(line.find(R"(path="C:\\a b")"), std::string::npos) << line;
        EXPECT_NE(line.find(R"(empty="")"), std::string::npos) << line;
        EXPECT_NE(line.find(R"(q="say \"hi\"")"), std::string::npos) << line;
        EXPECT_NE(line.find(R"(eq="a=b")"), std::string::npos) << line;
    }

    // A path without spaces is written as is: backslashes alone do not need quotes.
    TEST(LogFormat, PlainPathsStayUnquoted) {
        const std::string line = formatRecord(fixedRecord(Level::Info, "x", {{"log", "C:\\Users\\me\\a.log"}}));
        EXPECT_TRUE(line.ends_with(R"( log=C:\Users\me\a.log)")) << line;
    }

    // A record is always exactly one line, even with newlines in the message or a value.
    TEST(LogFormat, StaysOnOneLine) {
        const std::string line = formatRecord(fixedRecord(Level::Error, "two\nlines", {{"v", "a\r\nb\tc\x01"}}));
        EXPECT_EQ(line.find('\n'), std::string::npos) << line;
        EXPECT_EQ(line.find('\r'), std::string::npos) << line;
        EXPECT_NE(line.find(R"(two\nlines)"), std::string::npos) << line;
        EXPECT_NE(line.find(R"(v="a\r\nb\tc\x01")"), std::string::npos) << line;
    }

    // Level names are five characters wide so the columns line up.
    TEST(LogFormat, LevelNames) {
        EXPECT_EQ(levelName(Level::Trace), "TRACE");
        EXPECT_EQ(levelName(Level::Debug), "DEBUG");
        EXPECT_EQ(levelName(Level::Info), "INFO ");
        EXPECT_EQ(levelName(Level::Warning), "WARN ");
        EXPECT_EQ(levelName(Level::Error), "ERROR");
    }

    // Records below the minimum level never reach a sink.
    TEST(Logger, FiltersByMinimumLevel) {
        Logger logger;
        auto sink = std::make_shared<MemorySink>();
        logger.addSink(sink);
        logger.log(Level::Debug, "hidden");
        logger.log(Level::Info, "shown");
        logger.setMinLevel(Level::Trace);
        logger.log(Level::Trace, "now shown");
        ASSERT_EQ(sink->lines.size(), 2u);
        EXPECT_NE(sink->lines[0].find("shown"), std::string::npos);
        EXPECT_NE(sink->lines[1].find("now shown"), std::string::npos);
        EXPECT_TRUE(logger.enabled(Level::Trace));
    }

    // Every sink gets every line.
    TEST(Logger, FansOutToAllSinks) {
        Logger logger;
        auto first = std::make_shared<MemorySink>();
        auto second = std::make_shared<MemorySink>();
        logger.addSink(first);
        logger.addSink(second);
        logger.log(Level::Error, "boom", {{"code", 5}});
        ASSERT_EQ(first->lines.size(), 1u);
        EXPECT_EQ(first->lines, second->lines);
    }

    // Many threads logging at once: no line is lost or mixed with another.
    TEST(Logger, ConcurrentWritesStayWhole) {
        constexpr int threads = 8;
        constexpr int perThread = 500;
        Logger logger;
        auto sink = std::make_shared<MemorySink>();
        logger.addSink(sink);
        std::vector<std::jthread> workers;
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([&logger, t] {
                for (int i = 0; i < perThread; ++i) {
                    logger.log(Level::Info, "work", {{"worker", t}, {"seq", i}, {"end", "x"}});
                }
            });
        }
        workers.clear(); // Joins.
        ASSERT_EQ(sink->lines.size(), static_cast<std::size_t>(threads * perThread));
        std::set<std::string> seen;
        for (const std::string& line : sink->lines) {
            ASSERT_TRUE(line.ends_with(" end=x")) << line;
            const auto worker = line.find(" worker=");
            ASSERT_NE(worker, std::string::npos) << line;
            seen.insert(line.substr(worker, line.size() - worker));
        }
        EXPECT_EQ(seen.size(), static_cast<std::size_t>(threads * perThread)); // Each (worker, seq) once.
    }

}} // namespace icd::core::logging
