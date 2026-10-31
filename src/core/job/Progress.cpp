#include "core/job/Progress.h"

#include <algorithm>
#include <utility>

namespace icd::core {

    double Progress::fraction() const {
        if (total == 0) {
            return 0.0;
        }
        return std::clamp(static_cast<double>(done) / static_cast<double>(total), 0.0, 1.0);
    }

    ThrottledProgress::ThrottledProgress(std::shared_ptr<ProgressSink> target, std::chrono::milliseconds interval,
                                         Clock clock) :
        target_(std::move(target)), interval_(interval), clock_(std::move(clock)) {}

    namespace {

        // Two snapshots belong to the same stage when name and total match; a new total starts a new stage.
        bool sameStage(const Progress& a, const Progress& b) { return a.stage == b.stage && a.total == b.total; }

        // Whether progress is older than a snapshot already seen for the same stage. Workers compute and
        // report their counts concurrently, so a lower count can arrive after a higher one.
        bool olderThan(const Progress& progress, const std::optional<Progress>& seen) {
            return seen && sameStage(*seen, progress) && progress.done < seen->done;
        }

    } // namespace

    bool ThrottledProgress::mustForward(const Progress& progress) const {
        return !last_ || !sameStage(*last_, progress) || progress.finished();
    }

    void ThrottledProgress::report(const Progress& progress) {
        const std::scoped_lock lock(mutex_);
        if (olderThan(progress, last_) || olderThan(progress, pending_)) {
            return; // Stale: progress within a stage only moves forward.
        }
        const auto now = clock_();
        if (!mustForward(progress) && now - lastSent_ < interval_) {
            pending_ = progress; // Newest wins; delivered later.
            return;
        }
        forward(progress, now);
    }

    void ThrottledProgress::flush() {
        const std::scoped_lock lock(mutex_);
        if (pending_) {
            forward(*pending_, clock_());
        }
    }

    void ThrottledProgress::forward(const Progress& progress, std::chrono::steady_clock::time_point now) {
        // Called under the lock, so the target always sees reports in order.
        target_->report(progress);
        lastSent_ = now;
        last_ = progress;
        pending_.reset();
    }

} // namespace icd::core
