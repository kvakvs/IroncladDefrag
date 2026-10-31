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

    bool ThrottledProgress::mustForward(const Progress& progress) const {
        return !lastSent_ || lastStage_ != progress.stage || progress.finished();
    }

    void ThrottledProgress::report(const Progress& progress) {
        const std::scoped_lock lock(mutex_);
        const auto now = clock_();
        if (!mustForward(progress) && now - *lastSent_ < interval_) {
            pending_ = progress; // Newest wins; delivered later.
            return;
        }
        // Forwarded under the lock so the target always sees reports in order.
        target_->report(progress);
        lastSent_ = now;
        lastStage_ = progress.stage;
        pending_.reset();
    }

    void ThrottledProgress::flush() {
        const std::scoped_lock lock(mutex_);
        if (!pending_) {
            return;
        }
        target_->report(*pending_);
        lastSent_ = clock_();
        lastStage_ = pending_->stage;
        pending_.reset();
    }

} // namespace icd::core
