#include "core/job/StageClock.h"

#include <algorithm>

namespace icd::core {

    namespace {

        // Minimum share of a stage done before an estimate is shown; earlier ones jump around too much.
        constexpr double minimumFraction = 0.02;

        // Minimum time into a stage before an estimate is shown.
        constexpr std::chrono::seconds minimumStageTime{1};

    } // namespace

    StageClock::StageClock(TimePoint jobStart) : jobStart_(jobStart), stageStart_(jobStart), lastUpdate_(jobStart) {}

    void StageClock::update(const Progress& progress, TimePoint now) {
        if (!started_ || progress.stage != stage_ || progress.total != total_) {
            stage_ = progress.stage;
            total_ = progress.total;
            stageStart_ = now;
            started_ = true;
        }
        lastUpdate_ = now;
        fraction_ = progress.fraction();
    }

    std::chrono::seconds StageClock::elapsed(TimePoint now) const {
        return std::chrono::duration_cast<std::chrono::seconds>(now - jobStart_);
    }

    std::optional<std::chrono::seconds> StageClock::remaining(TimePoint now) const {
        const auto spent = lastUpdate_ - stageStart_;
        if (!started_ || total_ == 0 || fraction_ < minimumFraction || spent < minimumStageTime) {
            return std::nullopt;
        }
        const auto atUpdate = std::chrono::duration<double>(spent) * ((1.0 - fraction_) / fraction_);
        const auto left = atUpdate - (now - lastUpdate_);
        return std::chrono::seconds(
            static_cast<std::chrono::seconds::rep>(std::max(0.0, std::chrono::duration<double>(left).count())));
    }

} // namespace icd::core
