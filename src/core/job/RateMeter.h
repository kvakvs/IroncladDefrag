#pragma once

#include <chrono>
#include <deque>
#include <optional>

namespace icd::core {

    // A rate over the last few seconds of a growing total (bytes moved, worker busy time), for the Progress tab: a
    // whole-run average hides a slow patch, an instant rate jumps with every sample. Kept out of the UI so the
    // window rules are tested.
    class RateMeter {
    public:
        // Clock type; steady, like StageClock.
        using TimePoint = std::chrono::steady_clock::time_point;

        // Averages over `window` (5 s by default).
        explicit RateMeter(std::chrono::milliseconds window = std::chrono::seconds(5));

        // Takes the total at a time; samples older than the window are dropped, except the newest of them, which
        // anchors the rate. Totals that go down (a new run) restart the meter.
        void add(TimePoint now, double total);

        // Total gained per second over the window; nullopt until two samples at least 250 ms apart exist.
        [[nodiscard]] std::optional<double> perSecond() const;

        // Forgets every sample (a new job starts).
        void reset();

    private:
        // A total and when it was seen.
        struct Sample {
            TimePoint at; // When.
            double total = 0.0; // The total then.
        };

        std::chrono::milliseconds window_; // How far back the rate looks.
        std::deque<Sample> samples_; // Oldest first; the first may be older than the window.
    };

} // namespace icd::core
