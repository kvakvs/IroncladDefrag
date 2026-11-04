#pragma once

#include "core/job/RateMeter.h"
#include "services/execute/ExecuteJob.h"
#include "services/execute/FileDefrag.h"
#include "services/job/JobRunner.h"
#include "ui/CallbackTimer.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace icd::core {
    class PauseGate;
}

namespace icd::ui {

    class BottomPanel;
    struct ProgressDetails;

    // The GUI side of one execution (plan.md 11): builds its job, turns the job's feed into Progress tab fields (disk
    // throughput, time left from the bytes still to move, current file, move counts) and Log tab rows, and gives the
    // status line when it ends. Kept apart from WorkflowBinding, which only decides when a run starts and ends.
    // UI thread only.
    class ExecutionView {
    public:
        // Rows go to bottom's Log tab.
        explicit ExecutionView(BottomPanel& bottom);

        // Stops taking rows.
        ~ExecutionView();

        ExecutionView(const ExecutionView&) = delete;
        ExecutionView& operator=(const ExecutionView&) = delete;

        // A job that runs request with pause; the view follows it from now on, forgetting any earlier run.
        [[nodiscard]] services::JobFunction start(services::ExecuteJobRequest request,
                                                  std::shared_ptr<core::PauseGate> pause);

        // A job that defragments one file with pause (T128); followed like start()'s run.
        [[nodiscard]] services::JobFunction startFile(services::FileDefragRequest request,
                                                      std::shared_ptr<core::PauseGate> pause);

        // Whether the followed run is a single file (its status line says so).
        [[nodiscard]] bool fileRun() const { return fileRun_; }

        // Whether a run started by start() is followed.
        [[nodiscard]] bool active() const { return feed_ != nullptr; }

        // Overwrites the execution fields of details, the gauge fraction and the time left (from the bytes still to
        // move at the recent rate) once the moves run; revalidation reports like an analysis until then.
        void fill(ProgressDetails& details, std::optional<double>& fraction,
                  std::optional<std::chrono::seconds>& remaining, std::chrono::steady_clock::time_point now);

        // The run ended: its last rows and the summary go into the Log tab and the view stops following it. Returns
        // the status line, or nullopt when the job ended before a summary (it failed or was cancelled while the
        // volume was read again).
        [[nodiscard]] std::optional<std::string> finish(std::string_view drive);

        // The run continues after a pause: the paused seconds must not count against the throughput, which the time
        // left is computed from.
        void resumed() { throughput_.reset(); }

        // The feed of the followed run (null when none), for the live map.
        [[nodiscard]] const std::shared_ptr<services::ExecutionFeed>& feed() const { return feed_; }

    private:
        // Moves the feed's new lines into the Log tab.
        void takeLines();

        BottomPanel& bottom_; // Log tab.
        std::shared_ptr<services::ExecutionFeed> feed_; // The followed run; null when none.
        bool fileRun_ = false; // The followed run defragments one file (startFile()).
        core::RateMeter throughput_; // Bytes moved per second over the last seconds.
        CallbackTimer rows_; // Takes new rows while the run is followed, also while paused (no progress then).
    };

} // namespace icd::ui
