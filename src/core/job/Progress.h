#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace icd::core {

    // Snapshot of how far a job has come, for the progress bar, the status line and the log.
    struct Progress {
        std::string stage; // Current phase, e.g. "Reading MFT"; a change is always reported.
        std::uint64_t done = 0; // Work units finished in this stage.
        std::uint64_t total = 0; // Work units in this stage; 0 when unknown.
        std::string message; // Optional detail, e.g. the file being processed.

        // done / total clamped to [0, 1]; 0 when the total is unknown.
        [[nodiscard]] double fraction() const;

        // Whether the stage is complete (known total reached).
        [[nodiscard]] bool finished() const { return total != 0 && done >= total; }
    };

    // Receives progress from a job. Implementations must accept calls from several threads at once.
    class ProgressSink {
    public:
        virtual ~ProgressSink() = default;

        // Takes one snapshot.
        virtual void report(const Progress& progress) = 0;
    };

    // Rate-limits progress to a target (about 10 per second) so workers can report freely without flooding
    // the UI. The first report, stage changes and finished stages always pass; a report dropped in between
    // is kept and delivered by flush() or by the next report that passes the time check. Within a stage
    // progress only moves forward: a lower count arriving late from another worker is dropped.
    class ThrottledProgress : public ProgressSink {
    public:
        // Time source; tests pass a fake one.
        using Clock = std::function<std::chrono::steady_clock::time_point()>;

        // Forwards to target at most once per interval.
        explicit ThrottledProgress(
            std::shared_ptr<ProgressSink> target, std::chrono::milliseconds interval = std::chrono::milliseconds(100),
            Clock clock = [] { return std::chrono::steady_clock::now(); });

        // Forwards now or keeps the snapshot for later, depending on time and content.
        void report(const Progress& progress) override;

        // Delivers a held-back snapshot, e.g. when the job ends.
        void flush();

    private:
        // Whether progress must pass regardless of time (first report, new stage, finished stage).
        [[nodiscard]] bool mustForward(const Progress& progress) const;

        // Sends progress to the target and records it as the last one sent; called with mutex_ held.
        void forward(const Progress& progress, std::chrono::steady_clock::time_point now);

        std::shared_ptr<ProgressSink> target_; // Receiver of the throttled stream.
        std::chrono::milliseconds interval_; // Minimum time between forwarded reports.
        Clock clock_; // Time source.
        std::mutex mutex_; // Guards the fields below; reports come from workers.
        std::chrono::steady_clock::time_point lastSent_; // When last_ went out.
        std::optional<Progress> last_; // Last forwarded report; empty before the first.
        std::optional<Progress> pending_; // Newest report not yet forwarded.
    };

} // namespace icd::core
