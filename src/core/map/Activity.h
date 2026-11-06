#pragma once

#include "core/map/ColorModes.h"
#include "core/map/ColorScheme.h"
#include "core/map/Viewport.h"
#include "core/map/World.h"
#include "core/snapshot/Types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core {

    // What a pulse on the map shows (T114): where the disk is being read or written right now.
    enum class ActivityKind : std::uint8_t {
        Read, // Bars grow and fade out, as if the data lifted off the map.
        Write, // Bars shrink and fade in, as if the data landed on the map.
    };

    // How a pulse's bars look at one moment: drawn over the map about their centers, at an opacity. The height is in
    // window pixels so a pulse looks the same at every zoom (a multiple of the row height grew huge when zoomed in).
    struct PulseLook {
        double scaleX = 1.0; // Width scale; 1: the bar's own width.
        double height = 1.0; // Height in window pixels, whatever the zoom.
        double opacity = 0.0; // 0-1 over the map.
    };

    // Color scheme keys of the pulses: reads cool (white on the dark map, deep blue on the light ones), writes warm
    // (amber, orange) in the presets, so source and destination are told apart and both stand out from the map.
    inline constexpr std::string_view readKey = "activity.read";
    inline constexpr std::string_view writeKey = "activity.write";

    // The pulses' legend entries ("Reading", "Writing"): listed after every mode's own entries, so their colors can
    // be changed in any mode. They have no counts.
    [[nodiscard]] std::span<const LegendEntry> activityLegend();

    // The fill of read and write pulses.
    struct ActivityColors {
        Rgba read = 0; // Fill of read pulses.
        Rgba write = 0; // Fill of write pulses.
    };

    // The pulse colors of a scheme (the user's, else the preset's).
    [[nodiscard]] ActivityColors activityColors(const ColorScheme& scheme);

    // How long one pulse lasts.
    inline constexpr std::chrono::milliseconds pulseDuration{400};

    // The look of a pulse `elapsed` after its start: a read grows from its bar's width to 1.2x as wide and from 1 to
    // 16 pixels tall while it fades from 0.5 to 0.05 opacity, a write the reverse (the user's choice: a flat row piece
    // grows mostly up and down, so a whole-row bar does not cover the map sideways). Nullopt before the start and once
    // the pulse is over.
    [[nodiscard]] std::optional<PulseLook> pulseLook(ActivityKind kind, AnimationClock::duration elapsed);

    // The bars of a cluster run on the map: its piece of the first row, the full rows between as one block and its
    // piece of the last row (one, two or three cell rectangles, top to bottom). Clipped to the volume.
    [[nodiscard]] std::vector<CellRect> activityBars(const World& world, ClusterRun run);

    // Positions [from, to) along a sequence of runs, counted in clusters from the start of the first run.
    struct SequenceRange {
        ClusterCount from = 0; // First position.
        ClusterCount to = 0; // One past the last.
    };

    // A share of a job's work between two progress reports, as fractions of the whole.
    struct WorkShare {
        double from = 0.0; // Done at the earlier report.
        double to = 0.0; // Done at the later one.
    };

    // The clusters of `runs` (in order, as one sequence, e.g. a file's extents in VCN order) within `range`, as runs:
    // which part of the MFT a read between two progress reports covered.
    [[nodiscard]] std::vector<ClusterRun> runsBetween(std::span<const ClusterRun> runs, SequenceRange range);

    // One pulse: runs read or written, and when it starts (it may start in the future, so a batch is spread out).
    struct ActivityPulse {
        std::vector<ClusterRun> runs; // Where.
        ActivityKind kind = ActivityKind::Read; // Read or write.
        AnimationClock::time_point start{}; // When it starts.
    };

    // The pulses waiting or showing, bounded so a burst of tiny moves cannot pile up work for every frame. UI thread
    // only, no wx, so it is unit-tested.
    class ActivityLayer {
    public:
        // Most pulses kept; more are dropped (and counted) until some end.
        static constexpr std::size_t maxPulses = 256;

        // Most runs a pulse keeps (a file in thousands of fragments shows its first ones).
        static constexpr std::size_t maxRunsPerPulse = 16;

        // Adds a pulse; false when it was dropped (full, or nothing to show).
        bool add(ActivityPulse pulse);

        // Forgets pulses that are over by `now`.
        void prune(AnimationClock::time_point now);

        // Forgets every pulse (pause, another map).
        void clear() { pulses_.clear(); }

        // The pulses kept, in the order they were added.
        [[nodiscard]] std::span<const ActivityPulse> pulses() const { return pulses_; }

        // Whether nothing is waiting or showing.
        [[nodiscard]] bool empty() const { return pulses_.empty(); }

        // Pulses dropped because the layer was full, since it was made.
        [[nodiscard]] std::size_t dropped() const { return dropped_; }

    private:
        std::vector<ActivityPulse> pulses_; // Waiting or showing.
        std::size_t dropped_ = 0; // Dropped while full.
    };

} // namespace icd::core
