#pragma once

#include <chrono>
#include <cstddef>
#include <optional>

namespace icd::core {

    // A time-left figure for display: it changes at most once per period (3 s by default), to the average of the
    // estimates made since the last change, so it neither flickers with every progress report nor follows every
    // short burst or stall. Kept out of the UI so the rules are tested.
    class SmoothedEstimate {
    public:
        // Clock type; steady, like StageClock.
        using TimePoint = std::chrono::steady_clock::time_point;

        // Changes the shown value at most once per period.
        explicit SmoothedEstimate(std::chrono::milliseconds period = std::chrono::seconds(3));

        // Takes an estimate (nullopt: not known now, which is left out of the average) and returns what to show:
        // the first known estimate at once, later the average of each period's estimates.
        [[nodiscard]] std::optional<std::chrono::seconds> update(TimePoint now,
                                                                 std::optional<std::chrono::seconds> estimate);

        // Forgets everything (a new job or stage starts).
        void reset();

    private:
        std::chrono::milliseconds period_; // Shortest time between two changes.
        double sum_ = 0.0; // Seconds of the estimates since the last change.
        std::size_t count_ = 0; // How many.
        std::optional<std::chrono::seconds> shown_; // What is shown.
        TimePoint shownAt_{}; // When it was last changed.
    };

} // namespace icd::core
