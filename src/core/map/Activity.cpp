#include "core/map/Activity.h"

#include <algorithm>
#include <array>

namespace icd::core {

    namespace {

        // Size and opacity of a read pulse at its start and end; a write runs them backwards.
        constexpr double smallScale = 1.0;
        constexpr double largeScaleX = 1.2;
        constexpr double largeScaleY = 10.0;
        constexpr double strongOpacity = 0.5;
        constexpr double faintOpacity = 0.05;

        // a at t = 0 to b at t = 1.
        double lerp(double a, double b, double t) { return a + (b - a) * t; }

    } // namespace

    std::span<const LegendEntry> activityLegend() {
        static const std::array entries{
            LegendEntry{.label = "Reading", .color = presetColor(ColorPreset::Standard, readKey), .key = readKey},
            LegendEntry{.label = "Writing", .color = presetColor(ColorPreset::Standard, writeKey), .key = writeKey},
        };
        return entries;
    }

    ActivityColors activityColors(const ColorScheme& scheme) {
        return {.read = scheme.color(readKey), .write = scheme.color(writeKey)};
    }

    std::optional<PulseLook> pulseLook(ActivityKind kind, AnimationClock::duration elapsed) {
        if (elapsed < AnimationClock::duration::zero() || elapsed >= pulseDuration) {
            return std::nullopt;
        }
        const double t = std::chrono::duration<double>(elapsed) / std::chrono::duration<double>(pulseDuration);
        const double read = kind == ActivityKind::Read ? t : 1.0 - t; // How far along the read direction.
        return PulseLook{.scaleX = lerp(smallScale, largeScaleX, read),
                         .scaleY = lerp(smallScale, largeScaleY, read),
                         .opacity = lerp(strongOpacity, faintOpacity, read)};
    }

    std::vector<CellRect> activityBars(const World& world, ClusterRun run) {
        const Lcn end = std::min<Lcn>(run.start + run.length, world.clusters());
        if (run.length == 0 || run.start >= end) {
            return {};
        }
        const std::uint64_t width = world.rowWidth();
        const CellPoint first = world.cellOf(run.start);
        const CellPoint last = world.cellOf(end - 1);
        if (first.y == last.y) {
            return {{first.x, first.y, last.x - first.x + 1, 1}};
        }
        std::vector<CellRect> bars{{first.x, first.y, width - first.x, 1}};
        if (last.y > first.y + 1) {
            bars.push_back({0, first.y + 1, width, last.y - first.y - 1});
        }
        bars.push_back({0, last.y, last.x + 1, 1});
        return bars;
    }

    std::vector<ClusterRun> runsBetween(std::span<const ClusterRun> runs, SequenceRange range) {
        const ClusterCount from = range.from;
        const ClusterCount to = range.to;
        std::vector<ClusterRun> out;
        ClusterCount position = 0; // Where the current run starts in the sequence.
        for (const ClusterRun& run : runs) {
            const ClusterCount begin = std::max(from, position);
            const ClusterCount finish = std::min(to, position + run.length);
            if (begin < finish) {
                out.push_back({run.start + (begin - position), finish - begin});
            }
            position += run.length;
            if (position >= to) {
                break;
            }
        }
        return out;
    }

    bool ActivityLayer::add(ActivityPulse pulse) {
        std::erase_if(pulse.runs, [](const ClusterRun& run) { return run.length == 0; });
        if (pulse.runs.empty()) {
            return false;
        }
        if (pulses_.size() >= maxPulses) {
            ++dropped_;
            return false;
        }
        if (pulse.runs.size() > maxRunsPerPulse) {
            pulse.runs.resize(maxRunsPerPulse);
        }
        pulses_.push_back(std::move(pulse));
        return true;
    }

    void ActivityLayer::prune(AnimationClock::time_point now) {
        std::erase_if(pulses_, [now](const ActivityPulse& pulse) { return now - pulse.start >= pulseDuration; });
    }

} // namespace icd::core
