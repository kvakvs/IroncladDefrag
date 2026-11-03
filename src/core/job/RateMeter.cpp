#include "core/job/RateMeter.h"

namespace icd::core {

    namespace {

        // Shortest span a rate is computed over; shorter ones are mostly sampling noise.
        constexpr std::chrono::milliseconds minimumSpan{250};

    } // namespace

    RateMeter::RateMeter(std::chrono::milliseconds window) : window_(window) {}

    void RateMeter::add(TimePoint now, double total) {
        if (!samples_.empty() && (total < samples_.back().total || now < samples_.back().at)) {
            samples_.clear();
        }
        samples_.push_back({now, total});
        // Keep one sample at or before the window's start, so the rate always spans the whole window.
        while (samples_.size() > 2 && samples_[1].at <= now - window_) {
            samples_.pop_front();
        }
    }

    std::optional<double> RateMeter::perSecond() const {
        if (samples_.size() < 2) {
            return std::nullopt;
        }
        const Sample& first = samples_.front();
        const Sample& last = samples_.back();
        const auto span = last.at - first.at;
        if (span < minimumSpan) {
            return std::nullopt;
        }
        return (last.total - first.total) / std::chrono::duration<double>(span).count();
    }

    void RateMeter::reset() { samples_.clear(); }

} // namespace icd::core
