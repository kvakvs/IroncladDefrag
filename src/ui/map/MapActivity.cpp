#include "ui/map/MapActivity.h"

#include "core/log/Log.h"
#include "ui/map/MapCanvas.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace icd::ui {

    namespace {

        // Repaint interval while pulses show: like the navigator's, under a display frame, so the present (which
        // waits for the display) paces the frames; 16 ms met Windows' 15.6 ms timer steps and lost every third frame.
        constexpr int frameMs = 8;

        // Frames with pulses per frame-statistics log line.
        constexpr std::size_t framesPerLog = 3600;

        // The smallest bar drawn, in pixels: a row far under a pixel tall would show nothing.
        constexpr double minimumBar = 1.0;

        // A cell rectangle in window pixels, at least minimumBar on each side.
        PixelRect pixelsOf(const core::CellRect& cells, const core::Viewport& view) {
            const core::PixelPoint a = view.pixelAt({static_cast<double>(cells.x), static_cast<double>(cells.y)});
            const core::PixelPoint b =
                view.pixelAt({static_cast<double>(cells.x + cells.width), static_cast<double>(cells.y + cells.height)});
            PixelRect r{a.x, a.y, b.x - a.x, b.y - a.y};
            if (r.width < minimumBar) {
                r.x -= (minimumBar - r.width) / 2;
                r.width = minimumBar;
            }
            if (r.height < minimumBar) {
                r.y -= (minimumBar - r.height) / 2;
                r.height = minimumBar;
            }
            return r;
        }

        // Whether a bar scaled about its center reaches into the window.
        bool visible(const PixelRect& r, const core::PulseLook& look, const core::Viewport& view) {
            const double cx = r.x + r.width / 2;
            const double cy = r.y + r.height / 2;
            const double halfW = r.width * look.scaleX / 2;
            const double halfH = r.height * look.scaleY / 2;
            return cx + halfW > 0 && cx - halfW < view.size.x && cy + halfH > 0 && cy - halfH < view.size.y;
        }

        // The mean of values (0 for none).
        double mean(const std::vector<double>& values) {
            return values.empty()
                ? 0.0
                : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        }

        // The 95th percentile of values (0 for none).
        double p95(std::vector<double> values) {
            if (values.empty()) {
                return 0.0;
            }
            std::ranges::sort(values);
            return values[static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(values.size() - 1)))];
        }

    } // namespace

    MapActivity::MapActivity(MapCanvas& canvas) : canvas_(canvas), timer_([this] { tick(); }) {}

    MapActivity::~MapActivity() { timer_.Stop(); }

    void MapActivity::add(core::ActivityKind kind, std::vector<core::ClusterRun> runs,
                          core::AnimationClock::time_point start) {
        if (!shown_ || paused_ || !layer_.add({.runs = std::move(runs), .kind = kind, .start = start})) {
            return;
        }
        if (!timer_.IsRunning()) {
            intervals_.clear();
            paintTimes_.clear();
            lastFrame_.reset();
            timer_.Start(frameMs);
        }
    }

    void MapActivity::setShown(bool shown) {
        shown_ = shown;
        if (!shown) {
            clear();
        }
    }

    void MapActivity::setPaused(bool paused) {
        paused_ = paused;
        if (paused) {
            clear();
        }
    }

    void MapActivity::clear() {
        const bool had = !layer_.empty();
        layer_.clear();
        if (had) {
            canvas_.Refresh(false); // Pulses showing go away at once.
        }
    }

    bool MapActivity::draw(core::AnimationClock::time_point now, const core::World& world, const core::Viewport& view,
                           const BarPainter& paint) {
        bool drew = false;
        for (const core::ActivityPulse& pulse : layer_.pulses()) {
            if (const std::optional<core::PulseLook> look = core::pulseLook(pulse.kind, now - pulse.start); look) {
                drew = drawPulse(pulse, *look, world, view, paint) || drew; // Without a look: not started, or over.
            }
        }
        return drew;
    }

    bool MapActivity::drawPulse(const core::ActivityPulse& pulse, const core::PulseLook& look, const core::World& world,
                                const core::Viewport& view, const BarPainter& paint) const {
        const core::Rgba fill = pulse.kind == core::ActivityKind::Read ? colors_.read : colors_.write;
        bool drew = false;
        for (const core::ClusterRun& run : pulse.runs) {
            for (const core::CellRect& cells : core::activityBars(world, run)) {
                if (const PixelRect bar = pixelsOf(cells, view); visible(bar, look, view)) {
                    paint(bar, look, fill);
                    drew = true;
                }
            }
        }
        return drew;
    }

    void MapActivity::frameDrawn(core::AnimationClock::time_point start, double paintMs) {
        if (lastFrame_) {
            intervals_.push_back(std::chrono::duration<double, std::milli>(start - *lastFrame_).count());
        }
        lastFrame_ = start;
        paintTimes_.push_back(paintMs);
        if (paintTimes_.size() >= framesPerLog) {
            logFrames(); // A long run logs every minute or so instead of once at the end.
        }
    }

    void MapActivity::tick() {
        layer_.prune(core::AnimationClock::now());
        if (layer_.empty()) {
            timer_.Stop();
            logFrames();
        }
        canvas_.Refresh(false); // Also once after the last pulse, so it leaves no trace.
    }

    void MapActivity::logFrames() {
        if (paintTimes_.size() < 2) {
            return; // A lone pulse says nothing about the frame rate.
        }
        const double interval = mean(intervals_);
        core::logging::info("activity frames",
                            {{"frames", paintTimes_.size()},
                             {"fps", interval > 0 ? 1000.0 / interval : 0.0},
                             {"interval_p95_ms", p95(intervals_)},
                             {"paint_ms", mean(paintTimes_)},
                             {"paint_p95_ms", p95(paintTimes_)},
                             {"paint_max_ms", std::ranges::max(paintTimes_)},
                             {"dropped", layer_.dropped() - droppedLogged_}});
        droppedLogged_ = layer_.dropped();
        intervals_.clear();
        paintTimes_.clear();
        lastFrame_.reset();
    }

} // namespace icd::ui
