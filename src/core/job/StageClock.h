#pragma once

#include "core/job/Progress.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace icd::core {

    // Job and stage timing for the Progress tab: time since the job started and a linear estimate of the time
    // left in the current stage. Kept out of the UI so the estimate rules are tested.
    class StageClock {
    public:
        // Clock type; steady, so changes of the wall clock do not disturb estimates.
        using TimePoint = std::chrono::steady_clock::time_point;

        // Starts timing a job.
        explicit StageClock(TimePoint jobStart);

        // Takes a progress report; a new stage (other name or total) restarts the stage timer.
        void update(const Progress& progress, TimePoint now);

        // Time since the job started.
        [[nodiscard]] std::chrono::seconds elapsed(TimePoint now) const;

        // Time left in the current stage; nullopt while too little is known (under 2% done or under one
        // second into the stage, or an unknown total).
        [[nodiscard]] std::optional<std::chrono::seconds> remaining(TimePoint now) const;

    private:
        TimePoint jobStart_; // When the job began.
        TimePoint stageStart_; // When the current stage began (its first report).
        TimePoint lastUpdate_; // When the last report arrived.
        std::string stage_; // Current stage name.
        std::uint64_t total_ = 0; // Current stage total.
        double fraction_ = 0.0; // Share of the stage done at lastUpdate_.
        bool started_ = false; // Whether any report arrived.
    };

} // namespace icd::core
