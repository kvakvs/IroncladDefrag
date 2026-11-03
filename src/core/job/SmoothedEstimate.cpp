#include "core/job/SmoothedEstimate.h"

#include <cmath>

namespace icd::core {

    SmoothedEstimate::SmoothedEstimate(std::chrono::milliseconds period) : period_(period) {}

    std::optional<std::chrono::seconds> SmoothedEstimate::update(TimePoint now,
                                                                 std::optional<std::chrono::seconds> estimate) {
        if (estimate) {
            sum_ += static_cast<double>(estimate->count());
            ++count_;
        }
        const bool due = !shown_ || now - shownAt_ >= period_;
        if (due && count_ != 0) {
            shown_ = std::chrono::seconds(std::llround(sum_ / static_cast<double>(count_)));
            shownAt_ = now;
            sum_ = 0.0;
            count_ = 0;
        }
        return shown_;
    }

    void SmoothedEstimate::reset() {
        sum_ = 0.0;
        count_ = 0;
        shown_.reset();
        shownAt_ = {};
    }

} // namespace icd::core
