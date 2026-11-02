#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/execute/PlanReplay.h"
#include "services/execute/Executor.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icd::services {

    // A log that cannot be read: not a log, another format version, or damaged before its last line.
    class ExecutionLogError : public std::runtime_error {
    public:
        // Keeps the message.
        explicit ExecutionLogError(const std::string& message) : std::runtime_error(message) {}
    };

    // Format version of execution logs; another version is refused, never migrated.
    inline constexpr std::uint64_t executionLogVersion = 1;

    // The execution log (plan.md 10.4): an append-only JSON lines file under
    // %LOCALAPPDATA%\IroncladDefrag\logs\execute. It starts with the whole plan (a "plan" line, then one "move" line
    // per move, flushed before anything moves), then gets a "dropped" line per move revalidation took out, a "begin"
    // line before each move touches the disk and an "outcome" line after it, "event" lines (paused, resumed, cancelled,
    // resumed run) and an "end" line. Every line is written whole and flushed, so after a crash only the last line can
    // be cut short. With the plan in the log, a run can be resumed from the log plus a fresh read of the volume.
    class ExecutionLogWriter {
    public:
        // Starts a new log with the plan (throws std::runtime_error when the file cannot be written).
        ExecutionLogWriter(const std::filesystem::path& file, const core::execute::ExecutionPlan& plan);

        // Continues an existing log (a resumed run).
        explicit ExecutionLogWriter(const std::filesystem::path& file);

        // Not copied or moved: the hooks point at it.
        ExecutionLogWriter(const ExecutionLogWriter&) = delete;
        ExecutionLogWriter& operator=(const ExecutionLogWriter&) = delete;
        ExecutionLogWriter(ExecutionLogWriter&&) = delete;
        ExecutionLogWriter& operator=(ExecutionLogWriter&&) = delete;
        ~ExecutionLogWriter() = default;

        // Revalidation took a move out.
        void dropped(const core::execute::DroppedMove& move);

        // A move is about to touch the disk.
        void begin(std::uint32_t move);

        // A move ended.
        void outcome(const MoveRecord& record);

        // Pause, resume, cancel, a resumed run.
        void event(std::string_view what);

        // The run ended (completed or cancelled).
        void end(const ExecutionReport& report);

        // Hooks that write begin, outcome and event lines.
        [[nodiscard]] ExecutionHooks hooks();

        // The file.
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        // Opens the file, empty or for appending.
        static std::ofstream openFile(const std::filesystem::path& file, bool truncate);

        // Writes one line and flushes it.
        void line(const std::string& json);

        std::filesystem::path path_; // The log.
        std::ofstream out_; // Open for appending.
    };

    // What a move's outcome line says.
    struct LoggedOutcome {
        std::uint32_t move = 0; // Plan index.
        std::string outcome; // outcomeKey() or "dropped".
        std::string reason; // Why, if anything.
        core::ClusterCount clusters = 0; // Clusters moved.
    };

    // A log read back.
    struct ExecutionLogContents {
        core::execute::ExecutionPlan plan; // The plan it ran.
        std::vector<LoggedOutcome> outcomes; // Outcome and dropped lines in order (a resumed move can appear twice).
        std::vector<bool> settled; // Per move: has a final line (any outcome but partial, or dropped).
        std::optional<std::uint32_t> inFlight; // Begun last without an outcome: the move a crash interrupted.
        bool ended = false; // An end line closes the last run.
        bool cancelled = false; // The last end line says cancelled.
        bool truncated = false; // The last line was cut short (a crash while writing it); it was ignored.
    };

    // Parses a log. Throws ExecutionLogError for anything but a cut-short last line.
    [[nodiscard]] ExecutionLogContents parseExecutionLog(std::string_view text);

    // Reads and parses a log file.
    [[nodiscard]] ExecutionLogContents readExecutionLog(const std::filesystem::path& file);

    // The moves a resumed run still has to try: every move without a final line, in plan order. A move cut short or
    // interrupted is among them; revalidation finds how far it got.
    [[nodiscard]] std::vector<std::uint32_t> movesToResume(const ExecutionLogContents& log);

    // %LOCALAPPDATA%\IroncladDefrag\logs\execute.
    [[nodiscard]] std::filesystem::path executionLogDirectory();

    // A new log file name for a volume: "<Volume{GUID}>-<UTC yyyymmdd-hhmmss>.jsonl" in `directory`.
    [[nodiscard]] std::filesystem::path newExecutionLogPath(const std::filesystem::path& directory,
                                                            const std::string& volumeName);

    // The newest log of a volume in `directory` that has no end line (a run that crashed), if any.
    [[nodiscard]] std::optional<std::filesystem::path> unfinishedExecutionLog(const std::filesystem::path& directory,
                                                                              const std::string& volumeName);

} // namespace icd::services
