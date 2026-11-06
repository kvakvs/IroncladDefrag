#pragma once

#include "core/map/Activity.h"
#include "core/map/Viewport.h"
#include "core/map/World.h"
#include "ui/CallbackTimer.h"
#include "ui/map/D2DRenderer.h"

#include <functional>
#include <optional>
#include <vector>

namespace icd::ui {

    class MapCanvas;

    // Read and write pulses over the map (T114): where the disk is read or written right now. Keeps the pulses,
    // repaints the canvas at the display rate while one is waiting or showing (the canvas draws cached tiles only, no
    // tile renders again), and gives the canvas each bar to draw, filled in its pulse's color. Nothing is kept while
    // paused or while the View toggle is off; Windows' "Show animations" setting is not consulted (the user's choice:
    // the View toggle alone decides). UI thread only.
    class MapActivity {
    public:
        // Draws one pulse bar: its rectangle in window pixels (already sized for this moment), opacity and fill.
        using BarPainter = std::function<void(const PixelRect&, double opacity, core::Rgba)>;

        // Repaints canvas while pulses show.
        explicit MapActivity(MapCanvas& canvas);

        // Stops repainting.
        ~MapActivity();

        MapActivity(const MapActivity&) = delete;
        MapActivity& operator=(const MapActivity&) = delete;

        // Adds a pulse of runs read or written, starting at `start` (now or a little later); ignored while off.
        void add(core::ActivityKind kind, std::vector<core::ClusterRun> runs, core::AnimationClock::time_point start);

        // The View toggle: off drops every pulse.
        void setShown(bool shown);

        // Whether the View toggle is on.
        [[nodiscard]] bool shown() const { return shown_; }

        // Pause: no pulses until the run resumes.
        void setPaused(bool paused);

        // Drops every pulse (another map).
        void clear();

        // The fills of read and write pulses (from the map's color preset).
        void setColors(core::ActivityColors colors) { colors_ = colors; }

        // Draws the bars of the pulses showing at `now` over the map; true when it drew any.
        bool draw(core::AnimationClock::time_point now, const core::World& world, const core::Viewport& view,
                  const BarPainter& paint);

        // A frame that drew pulses took `paintMs`, for the frame statistics logged when the pulses stop.
        void frameDrawn(core::AnimationClock::time_point start, double paintMs);

    private:
        // Draws the bars of one pulse with a look; true when any was in the window.
        bool drawPulse(const core::ActivityPulse& pulse, const core::PulseLook& look, const core::World& world,
                       const core::Viewport& view, const BarPainter& paint) const;

        // Timer: forgets pulses that are over, repaints, stops (and logs the frames) when none is left.
        void tick();

        // Logs the frame statistics of the pulses since the timer started, then forgets them.
        void logFrames();

        MapCanvas& canvas_; // Repainted while pulses show.
        core::ActivityLayer layer_; // The pulses.
        CallbackTimer timer_; // Repaints while pulses show.
        bool shown_ = true; // View toggle.
        bool paused_ = false; // The run is paused.
        core::ActivityColors colors_ = core::activityColors(core::ColorScheme()); // Fills of the pulses.
        std::vector<double> intervals_; // Milliseconds between frames that drew pulses.
        std::vector<double> paintTimes_; // Milliseconds those frames took to paint.
        std::optional<core::AnimationClock::time_point> lastFrame_; // Start of the previous frame with pulses.
        std::size_t droppedLogged_ = 0; // Dropped pulses already in the log.
    };

} // namespace icd::ui
