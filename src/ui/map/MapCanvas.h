#pragma once

#include "core/map/MapData.h"
#include "core/map/Palette.h"
#include "core/map/Viewport.h"
#include "core/util/LruCache.h"
#include "ui/map/D2DRenderer.h"

#include <wx/window.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace icd::core {
    class Executor;
}

namespace icd::ui {

    class MapNavigator;
    struct TileMailbox;

    // The drive map widget (plan.md 7.3): composes 256 x 256 tiles with Direct2D, renders missing tiles on the
    // shared pool and keeps the uploaded ones in an LRU cache with a memory cap. While exact tiles render, the
    // nearest cached coarser tile is drawn scaled. The paint path never touches analysis data. The world is
    // fixed per map, so resizing changes how much is visible, never the layout (plan.md 7.1). Input and
    // animation live in MapNavigator.
    class MapCanvas : public wxWindow {
    public:
        // GPU memory for cached tiles (1,024 tiles of 256 KB).
        static constexpr std::size_t tileCacheBytes = std::size_t{256} << 20U;

        // Creates an empty map; tiles render on executor, which must outlive the canvas.
        MapCanvas(wxWindow* parent, core::Executor& executor);

        // Stops delivery of tiles still rendering.
        ~MapCanvas() override;

        MapCanvas(const MapCanvas&) = delete;
        MapCanvas& operator=(const MapCanvas&) = delete;

        // Shows a map (null clears it) and fits the whole volume into the window.
        void setMap(std::shared_ptr<const core::MapData> map);

        // Changes the color mode; every tile renders again.
        void setPalette(std::shared_ptr<const core::Palette> palette);

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const { return map_ != nullptr; }

        // The current view.
        [[nodiscard]] const core::Viewport& viewport() const { return view_; }

        // Shows a view (clamped to the limits) and repaints.
        void setViewport(const core::Viewport& view);

        // How far the view may zoom and move in the current window.
        [[nodiscard]] core::ViewLimits limits() const;

        // The view showing the whole volume, or nullopt without a map.
        [[nodiscard]] std::optional<core::Viewport> fittedView() const;

        // Input and animation of this map.
        [[nodiscard]] MapNavigator& navigator() { return *navigator_; }

        // Starts recording frame times (an animation begins).
        void beginFrameStats();

        // Logs the frame times recorded since beginFrameStats() (an animation ended).
        void endFrameStats();

        // Takes a rendered tile on the UI thread (called through the mailbox).
        void deliver(std::uint64_t generation, core::TileId tile, std::vector<core::Rgba>&& pixels);

    private:
        // Draws the frame.
        void onPaint(wxPaintEvent& event);

        // Fits the first time the window gets a real size; afterwards keeps scale and center (no re-flow).
        void onSize(wxSizeEvent& event);

        // Map keys: zoom keys post View commands, panning keys go to the navigator.
        void onChar(wxKeyEvent& event);

        // Draws the visible tiles of the current level and asks for the missing ones.
        void drawTiles();

        // Draws the nearest cached coarser tile scaled into a missing tile's place; false when none is cached.
        bool drawFallback(core::TileId tile, const PixelRect& destination);

        // Queues a tile on the pool unless it is cached, already rendering, or too many are in flight.
        void request(core::TileId tile);

        // Forgets all tiles (new map, new palette, lost device); late results are dropped by generation.
        void resetTiles();

        // Uploads up to a per-frame budget of arrived tiles; repaints again when more are waiting.
        void uploadArrived();

        // A rendered tile waiting for upload.
        struct ArrivedTile {
            core::TileId tile; // Which tile.
            std::vector<core::Rgba> pixels; // Its pixels.
        };

        core::Executor& executor_; // Renders tiles.
        std::unique_ptr<D2DRenderer> renderer_; // Created on first paint.
        std::unique_ptr<MapNavigator> navigator_; // Input and animation.
        core::LruCache<core::TileId, GpuTileHandle, core::TileIdHash> cache_{tileCacheBytes}; // Uploaded tiles.
        std::unordered_set<core::TileId, core::TileIdHash> pending_; // Tiles rendering now.
        std::deque<ArrivedTile> arrived_; // Rendered, not yet uploaded (uploads are spread over frames).
        std::shared_ptr<TileMailbox> mailbox_; // Route from workers back to this window.
        std::shared_ptr<const core::MapData> map_; // What is shown; null for no map.
        std::shared_ptr<const core::Palette> palette_; // Color mode.
        core::Viewport view_; // Current view.
        std::uint64_t generation_ = 0; // Bumped whenever cached tiles become invalid.
        bool fitted_ = false; // Whether the current map was fitted to a real window size yet.
        std::vector<double> frameIntervals_; // Milliseconds between frames while animating.
        std::vector<double> paintTimes_; // Milliseconds spent painting each of those frames.
        std::optional<core::AnimationClock::time_point> lastFrame_; // Start of the previous animated frame.
        bool recording_ = false; // Whether frame times are being recorded.
    };

} // namespace icd::ui
