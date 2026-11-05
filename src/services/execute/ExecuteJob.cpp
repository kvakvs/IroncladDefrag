#include "services/execute/ExecuteJob.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"

#include <array>
#include <format>
#include <stdexcept>
#include <utility>

namespace icd::services {

    namespace {

        // A reason key and its words.
        struct ReasonWords {
            std::string_view key; // As the executor, the move primitive or revalidation reports it.
            std::string_view words; // For the Log tab.
        };

        constexpr std::array reasonWords{
            ReasonWords{"file-changed", "the file changed since it was planned"},
            ReasonWords{"file-gone", "the file no longer exists"},
            ReasonWords{"destination-in-use",
                        "its destination is taken and no other free space in its zone holds the rest"},
            ReasonWords{"earlier-move-dropped", "an earlier move of this file was dropped"},
            ReasonWords{"access-denied", "access denied"},
            ReasonWords{"locked", "the file is in use and locked"},
            ReasonWords{"unsupported", "the file system does not move this file"},
            ReasonWords{"retry", "the file system kept refusing the move for the moment"},
            ReasonWords{"failed", "the move failed"},
            ReasonWords{"open-failed", "the file could not be opened"},
            ReasonWords{"cancelled", "cancelled"},
        };

        // The path of a plan's move, or an empty text for an index it does not have.
        std::string pathOf(const core::execute::ExecutionPlan& plan, std::uint32_t move) {
            return move < plan.paths.size() ? plan.paths[move] : std::string();
        }

        // The reason of a record in words, with the Windows error code when there is one.
        std::string recordReason(const MoveRecord& record) {
            std::string out = reasonText(record.reason);
            if (record.win32Error != 0) {
                out += std::format(" (Windows error {})", record.win32Error);
            }
            return out;
        }

        // A line about the whole run.
        ExecutionLogLine runLine(std::string details) {
            return {.file = {}, .result = "run", .details = std::move(details)};
        }

        // A line stamped with the current time.
        ExecutionLogLine stampedNow(ExecutionLogLine line) {
            line.at = std::chrono::system_clock::now();
            return line;
        }

        // Moves that did not go as planned in a summary.
        std::uint64_t problems(const ExecutionSummary& s) { return s.totals.skipped + s.totals.partial + s.dropped; }

        // "12 moves done (1.2 GB)".
        std::string movedText(const ExecutionSummary& s) {
            const std::uint64_t moves = s.totals.moved + s.totals.alreadyDone;
            return std::format("{} {} done ({})", core::formatCount(moves), moves == 1 ? "move" : "moves",
                               core::formatBytes(s.totals.clustersMoved * s.bytesPerCluster));
        }

    } // namespace

    std::string reasonText(std::string_view key) {
        for (const ReasonWords& entry : reasonWords) {
            if (entry.key == key) {
                return std::string(entry.words);
            }
        }
        return key.empty() ? std::string("no reason given") : std::string(key);
    }

    std::optional<ExecutionLogLine> recordLine(const MoveRecord& record, const core::execute::ExecutionPlan& plan) {
        ExecutionLogLine line{.file = pathOf(plan, record.move), .result = {}, .details = {}};
        switch (record.outcome) {
        case MoveOutcome::Skipped:
            line.result = "skipped";
            line.details = recordReason(record);
            return line;
        case MoveOutcome::Partial:
            line.result = "partly moved";
            line.details = std::format("{} clusters moved, then it stopped: {}; the file is intact",
                                       core::formatCount(record.clustersMoved), recordReason(record));
            return line;
        case MoveOutcome::Moved:
        case MoveOutcome::AlreadyDone:
            break;
        }
        if (!record.verified) {
            line.result = "not verified";
            line.details = "its runs read back afterwards differ from where it was moved";
            return line;
        }
        if (record.replaced) {
            line.result = "moved elsewhere";
            line.details = "its destination was taken; it went to other free space in its zone";
            return line;
        }
        return std::nullopt;
    }

    ExecutionLogLine dropLine(const core::execute::DroppedMove& drop, const core::execute::ExecutionPlan& plan) {
        return {.file = pathOf(plan, drop.move),
                .result = "dropped",
                .details = "not run: " + reasonText(core::execute::dropKey(drop.reason))};
    }

    std::vector<ExecutionLogLine> summaryLines(const ExecutionSummary& s) {
        if (s.refused) {
            return {runLine("the plan was not run: " + *s.refused)};
        }
        std::vector<ExecutionLogLine> out;
        out.push_back(runLine(std::format("{} in {:.1f} s; {} skipped, {} partly moved, {} dropped before the start",
                                          movedText(s), s.seconds, core::formatCount(s.totals.skipped),
                                          core::formatCount(s.totals.partial), core::formatCount(s.dropped))));
        if (s.cancelled) {
            out.push_back(runLine("cancelled: every file is intact; the moves not run are kept in the execution log"));
        }
        if (!s.snapshotNote.empty()) {
            out.push_back(runLine(s.snapshotNote));
        }
        if (!s.log.empty()) {
            out.push_back(runLine("execution log: " + s.log.string()));
        }
        return out;
    }

    std::string summaryStatus(const ExecutionSummary& s, std::string_view drive, std::string_view what) {
        if (s.refused) {
            return std::format("The plan for {} was not run: {}", drive, *s.refused);
        }
        const std::string done = movedText(s);
        if (s.cancelled) {
            return std::format("Execution on {} cancelled after {:.1f} s: {}; every file is intact (see Log)", drive,
                               s.seconds, done);
        }
        if (problems(s) == 0) {
            return std::format("{} on {} in {:.1f} s: {}", what, drive, s.seconds, done);
        }
        return std::format("{} on {} in {:.1f} s: {}; {} did not go as planned (see Log)", what, drive, s.seconds, done,
                           core::formatCount(problems(s)));
    }

    std::shared_ptr<const core::execute::ExecutionPlan> ExecutionFeed::plan() const {
        const std::scoped_lock lock(mutex_);
        return plan_;
    }

    ExecutionCounters ExecutionFeed::counters() const {
        const std::scoped_lock lock(mutex_);
        return counters_;
    }

    std::vector<ExecutionLogLine> ExecutionFeed::takeLines() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(lines_, {});
    }

    std::vector<MoveRecord> ExecutionFeed::takeMoved() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(moved_, {});
    }

    std::optional<ExecutionSummary> ExecutionFeed::summary() const {
        const std::scoped_lock lock(mutex_);
        return summary_;
    }

    std::shared_ptr<const core::Snapshot> ExecutionFeed::takeVolumeNow() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(volumeNow_, nullptr);
    }

    void ExecutionFeed::setPlan(std::shared_ptr<const core::execute::ExecutionPlan> plan) {
        const std::scoped_lock lock(mutex_);
        plan_ = std::move(plan);
    }

    void ExecutionFeed::addLine(ExecutionLogLine line) {
        if (linesAdded_ >= maxLines) {
            ++linesOver_;
            return;
        }
        ++linesAdded_;
        line.at = std::chrono::system_clock::now();
        lines_.push_back(std::move(line));
    }

    void ExecutionFeed::revalidated(const Revalidation& revalidation) {
        // Copied before taking the lock: on a large volume this takes a while, and the UI reads counters meanwhile.
        // A copy, since the run patches revalidation's snapshot into the one it saves afterwards.
        auto now = revalidation.current != nullptr && !revalidation.refused
            ? std::make_shared<const core::Snapshot>(*revalidation.current)
            : nullptr;
        const std::scoped_lock lock(mutex_);
        if (plan_ == nullptr || revalidation.refused) {
            return;
        }
        const core::execute::ReplayResult& replay = revalidation.replay;
        volumeNow_ = std::move(now);
        counters_.moving = true;
        counters_.moves = replay.kept.size();
        counters_.dropped = replay.dropped.size();
        counters_.bytesPending = replay.pendingClusters * plan_->identity.geometry.bytesPerCluster;
        for (const core::execute::DroppedMove& drop : replay.dropped) {
            addLine(dropLine(drop, *plan_));
        }
    }

    ExecutionHooks ExecutionFeed::hooks() {
        ExecutionHooks h;
        h.onBegin = [this](std::uint32_t move) { begin(move); };
        h.onRecord = [this](const MoveRecord& r) { record(r); };
        h.onEvent = [this](std::string_view event) {
            const std::scoped_lock lock(mutex_);
            addLine(runLine(std::string(event)));
        };
        return h;
    }

    void ExecutionFeed::begin(std::uint32_t move) {
        const std::scoped_lock lock(mutex_);
        counters_.currentFile = plan_ != nullptr ? pathOf(*plan_, move) : std::string();
    }

    void ExecutionFeed::record(const MoveRecord& r) {
        const std::scoped_lock lock(mutex_);
        ExecutionCounters& c = counters_;
        ++c.finished;
        c.moved += r.outcome == MoveOutcome::Moved || r.outcome == MoveOutcome::AlreadyDone ? 1 : 0;
        c.skipped += r.outcome == MoveOutcome::Skipped ? 1 : 0;
        c.partial += r.outcome == MoveOutcome::Partial ? 1 : 0;
        if (plan_ == nullptr) {
            return;
        }
        c.bytesMoved += r.clustersMoved * plan_->identity.geometry.bytesPerCluster;
        if (auto line = recordLine(r, *plan_); line) {
            addLine(std::move(*line));
        }
        if (r.clustersMoved != 0) {
            moved_.push_back(r);
        }
    }

    void ExecutionFeed::finish(const ExecutionOutcome& outcome) {
        const std::scoped_lock lock(mutex_);
        ExecutionSummary s;
        s.refused = outcome.revalidation.refused;
        s.totals = outcome.report.totals;
        s.dropped = outcome.revalidation.replay.dropped.size();
        s.cancelled = outcome.report.cancelled;
        s.seconds = outcome.report.seconds;
        if (const core::execute::ExecutionPlan* plan = plan_.get(); plan != nullptr) {
            s.bytesPerCluster = plan->identity.geometry.bytesPerCluster;
        }
        s.log = outcome.log;
        s.snapshotNote = outcome.snapshotNote;
        summary_ = std::move(s);
        if (linesOver_ != 0) {
            lines_.push_back(
                stampedNow(runLine(std::format("{} more files that did not move as planned are listed in the "
                                               "execution log only",
                                               core::formatCount(linesOver_)))));
        }
    }

    void runGuiExecution(const core::DriveInfo& drive, core::execute::ExecutionPlan plan, const ExecuteOptions& options,
                         ExecutionFeed& feed, core::PauseGate* pause, JobContext& context) {
        auto run = std::make_shared<ExecutionRequest>(freshExecution(drive, std::move(plan)));
        run->options = options;
        runGuiRequest(run, feed, pause, context);
    }

    void runGuiRequest(const std::shared_ptr<ExecutionRequest>& run, ExecutionFeed& feed, core::PauseGate* pause,
                       JobContext& context) {
        run->observers = feed.hooks();
        run->onRevalidated = [&feed](const Revalidation& v) { feed.revalidated(v); };
        feed.setPlan(std::shared_ptr<const core::execute::ExecutionPlan>(run, &run->plan));
        const ExecutionOutcome outcome = runExecution(*run, context, pause);
        feed.finish(outcome);
        if (outcome.revalidation.refused) {
            throw std::runtime_error("the plan cannot run: " + *outcome.revalidation.refused);
        }
    }

    JobFunction makeExecuteJob(ExecuteJobRequest request, std::shared_ptr<ExecutionFeed> feed,
                               std::shared_ptr<core::PauseGate> pause) {
        return [r = std::move(request), feed = std::move(feed), pause = std::move(pause)](JobContext& context) {
            if (const auto& issues = r.outcome->levels.at(static_cast<std::size_t>(r.level)).issues; !issues.empty()) {
                throw std::runtime_error("the plan failed validation: " + issues.front());
            }
            context.progress.report({.stage = "Preparing the moves", .done = 0, .total = 0, .message = {}});
            runGuiExecution(r.drive, executionPlanFor(*r.outcome, *r.plan, r.drive, r.level), r.options, *feed,
                            pause.get(), context);
        };
    }

    JobFunction makeResumeJob(ResumeJobRequest request, std::shared_ptr<ExecutionFeed> feed,
                              std::shared_ptr<core::PauseGate> pause) {
        return [r = std::move(request), feed = std::move(feed), pause = std::move(pause)](JobContext& context) {
            context.progress.report({.stage = "Reading the execution log", .done = 0, .total = 0, .message = {}});
            auto run = std::make_shared<ExecutionRequest>(resumedExecution(r.drive, r.log));
            if (run->order.empty()) {
                throw std::runtime_error("the run in " + r.log.filename().string() + " has no moves left");
            }
            run->options = r.options;
            runGuiRequest(run, *feed, pause.get(), context);
        };
    }

} // namespace icd::services
