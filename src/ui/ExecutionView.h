#pragma once

#include "core/job/RateMeter.h"
#include "services/execute/ExecuteJob.h"
#include "services/job/JobRunner.h"

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

        // A job that runs request with pause; the view follows it from now on, forgetting any earlier run.
        [[nodiscard]] services::JobFunction start(services::ExecuteJobRequest request,
                                                  std::shared_ptr<core::PauseGate> pause);

        // Whether a run started by start() is followed.
        [[nodiscard]] bool active() const { return feed_ != nullptr; }

        // Overwrites the execution fields of details and the gauge fraction once the moves run (revalidation reports
        // like an analysis until then), and moves new rows into the Log tab.
        void fill(ProgressDetails& details, std::optional<double>& fraction, std::chrono::steady_clock::time_point now);

        // The run ended: its last rows and the summary go into the Log tab and the view stops following it. Returns
        // the status line, or nullopt when the job ended before a summary (it failed or was cancelled while the
        // volume was read again).
        [[nodiscard]] std::optional<std::string> finish(std::string_view drive);

        // The feed of the followed run (null when none), for the live map.
        [[nodiscard]] const std::shared_ptr<services::ExecutionFeed>& feed() const { return feed_; }

    private:
        // Moves the feed's new lines into the Log tab.
        void takeLines();

        BottomPanel& bottom_; // Log tab.
        std::shared_ptr<services::ExecutionFeed> feed_; // The followed run; null when none.
        core::RateMeter throughput_; // Bytes moved per second over the last seconds.
    };

} // namespace icd::ui
