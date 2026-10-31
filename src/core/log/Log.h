#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace icd::core::logging {

    // Severity of a log record; records below the logger's minimum level are dropped before formatting.
    enum class Level : std::uint8_t {
        Trace, // Very detailed tracing, off by default.
        Debug, // Diagnostics for developers.
        Info, // Normal milestones: start-up, job start and finish.
        Warning, // Something unexpected that the program recovered from.
        Error, // An operation failed.
    };

    // Name of a field. Only string literals convert to it (consteval), so field names stay fixed
    // identifiers that log readers can rely on, and a key can never be swapped with a value by mistake.
    struct FieldKey {
        // Accepts a string literal; anything else fails to compile.
        consteval FieldKey(const char* literal) : name(literal) {}

        std::string_view name; // Points into the literal, which lives for the whole program.
    };

    // One key=value detail of a record, so logs can be filtered and parsed by field instead of by prose.
    struct Field {
        // Text value.
        Field(FieldKey k, std::string_view v) : key(k.name), value(v) {}

        // Keeps string literals away from the bool/integer constructor.
        Field(FieldKey k, const char* v) : key(k.name), value(v) {}

        // Number or bool value, written as decimal or true/false.
        template <typename T>
            requires std::integral<T> || std::floating_point<T>
        Field(FieldKey k, T v) : key(k.name), value(formatNumber(v)) {}

        std::string_view key; // Field name: a short identifier without spaces (from a literal).
        std::string value; // Field value as text; quoted on output when it needs to be.

    private:
        // Formats arithmetic values; bool is spelled out so it is not read as a count.
        template <typename T>
        static std::string formatNumber(T v) {
            if constexpr (std::same_as<T, bool>) {
                return v ? "true" : "false";
            }
            else {
                return std::format("{}", v);
            }
        }
    };

    // Everything known about one log event, captured at the call site and handed to the sinks.
    struct Record {
        std::chrono::system_clock::time_point time; // When the event happened.
        Level level = Level::Info; // Severity.
        std::thread::id thread; // Logging thread, to follow work across the thread pool.
        std::string message; // Short event text, e.g. "startup".
        std::vector<Field> fields; // Structured details.
    };

    // Destination for formatted log lines (file, debugger, test buffer). Called with the logger's lock held.
    class Sink {
    public:
        virtual ~Sink() = default;

        // Writes one formatted UTF-8 line without a trailing newline. Must not throw.
        virtual void write(const Record& record, std::string_view line) = 0;

        // Pushes buffered output to its destination.
        virtual void flush() {}
    };

    // Thread-safe fan-out of records to sinks; one lock keeps lines from different threads whole.
    class Logger {
    public:
        // Adds a destination; records logged after this call reach it.
        void addSink(std::shared_ptr<Sink> sink);

        // Removes all sinks, e.g. at shutdown or between tests.
        void clearSinks();

        // Records below this level are dropped. The default is Info.
        void setMinLevel(Level level);

        // Whether a record at this level would be written, to skip building expensive fields.
        [[nodiscard]] bool enabled(Level level) const;

        // Stamps time and thread, formats once and writes the line to every sink.
        void log(Level level, std::string_view message, std::initializer_list<Field> fields = {});

        // Writes a record whose time and thread are already set (tests and replays).
        void write(const Record& record);

        // Flushes every sink.
        void flush();

    private:
        mutable std::mutex mutex_; // Guards sinks_ and serializes writes.
        std::vector<std::shared_ptr<Sink>> sinks_; // Destinations in the order they were added.
        std::atomic<Level> minLevel_{Level::Info}; // Records below this level are dropped.
    };

    // Upper-case level name padded to five characters, so columns line up in the log file.
    std::string_view levelName(Level level);

    // One log line: "2026-10-31T19:10:05.123Z INFO  t=1234 startup app=IroncladDefrag".
    // Control characters are escaped and field values quoted when needed, so a record is always one line.
    std::string formatRecord(const Record& record);

    // Process-wide logger used by the helpers below.
    Logger& globalLogger();

    // Shorthands that log to globalLogger() at one level.
    void trace(std::string_view message, std::initializer_list<Field> fields = {});
    void debug(std::string_view message, std::initializer_list<Field> fields = {});
    void info(std::string_view message, std::initializer_list<Field> fields = {});
    void warning(std::string_view message, std::initializer_list<Field> fields = {});
    void error(std::string_view message, std::initializer_list<Field> fields = {});

} // namespace icd::core::logging
