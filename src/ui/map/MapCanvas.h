#pragma once

#include "core/map/MapData.h"
#include "core/map/Palette.h"
#include "core/map/Viewport.h"
#include "core/util/LruCache.h"
#include "ui/map/D2DRenderer.h"
#include "ui/map/Minimap.h"

#include <wx/window.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace icd::core {
    class Executor;
    struct SelectionOverlay;
    struct MapOverlay;
} // namespace icd::core

namespace icd::ui {

    class MapHover;
    class MapNavigator;
    class MapSelection;
    struct TileMailbox;

    // What a rendered tile was drawn with, so late results for an older map or selection are recognised.
    struct RenderStamp {
        std::uint64_t generation = 0; // MapCanvas generation: map and device.
        std::uint64_t selection = 0; // Content version: selection and palette.
    };

    // The drive map widget (plan.md 7.3): composes 256 x 256 tiles with Direct2D, renders missing tiles on the
    // shared pool and keeps the uploaded ones in an LRU cache with a memory cap. While exact tiles render, the
    // nearest cached coarser tile is drawn scaled. The paint path never touches analysis data. The world is
    // fixed per map, so resizing changes how much is visible, never the layout (plan.md 7.1). Input and
    // animation live in MapNavigator, the hover tooltip in MapHover, the selected file in MapSelection. The
    // selection highlight is painted into tiles; changing it re-renders only the tiles it touches, showing the
    // old ones until the new ones arrive. A new color mode re-renders the cached tiles the same way; the map data
    // stays as it is.
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

        // Shows newer data of the same volume (an analysis still reading files): with the same world the view stays
        // and cached tiles stay on screen until they render again from the new data; otherwise like setMap().
        void updateMap(std::shared_ptr<const core::MapData> map);

        // Shows newer data of the same volume where only the clusters in `dirty` may look different (moves during
        // execution, plan.md 7.2): only the cached and rendering tiles over them render again, the selection follows
        // its file and the hover stays. Another world falls back to setMap().
        void updateMap(std::shared_ptr<const core::MapData> map, std::span<const core::ClusterRun> dirty);

        // Changes the color mode: cached tiles are marked dirty and render again, shown in the old colors until
        // then.
        void setPalette(std::shared_ptr<const core::Palette> palette);

        // Highlights a selection (null: none). Only cached or rendering tiles that the old or the new selection
        // touches are marked dirty and render again.
        void setSelection(std::shared_ptr<const core::SelectionOverlay> overlay);

        // Paints a plan-preview or zone-fit overlay over every tile (null: none); every tile renders again.
        void setOverlay(std::shared_ptr<const core::MapOverlay> overlay);

        // The selected file and fragment navigation.
        [[nodiscard]] MapSelection& selection() { return *selection_; }

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const { return map_ != nullptr; }

        // The map shown, or null; for widgets that describe the view (rulers).
        [[nodiscard]] const core::MapData* map() const { return map_.get(); }

        // The map shown, shared (an analysis of the same volume updates it instead of building anew).
        [[nodiscard]] std::shared_ptr<const core::MapData> sharedMap() const { return map_; }

        // Calls observer after every change of the view or the map, so rulers can follow; empty to stop.
        void setViewObserver(std::function<void()> observer) { viewObserver_ = std::move(observer); }

        // Shows or hides the minimap.
        void showMinimap(bool shown);

        // The minimap overlay, for input handling.
        [[nodiscard]] Minimap& minimap() { return minimap_; }

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

        // Tiles of the exact level that were not ready in the last frame (a coarser stand-in was shown); for
        // benchmarks.
        [[nodiscard]] std::size_t missingTiles() const { return missingTiles_; }

        // Takes a rendered tile on the UI thread (called through the mailbox).
        void deliver(RenderStamp stamp, core::TileId tile, std::vector<core::Rgba>&& pixels);

    private:
        // Marks every cached and rendering tile dirty: they stay on screen and render again.
        void markAllDirty();

        // Marks the cached and rendering tiles (and the minimap's tile) over any of runs dirty.
        void markDirty(std::span<const core::ClusterRun> runs);

        // Whether map has the same world as the map shown, so the view and the cached tiles can stay.
        [[nodiscard]] bool sameWorld(const core::MapData* map) const;

        // Draws the frame.
        void onPaint(wxPaintEvent& event);

        // Fits the first time the window gets a real size; afterwards keeps scale and center (no re-flow).
        void onSize(wxSizeEvent& event);

        // Map keys: zoom keys post View commands, panning keys go to the navigator.
        void onChar(wxKeyEvent& event);

        // Draws the visible tiles of the current level (faded over the next coarser one where cells are under a
        // pixel, see core::LevelBlend) and asks for the missing ones.
        void drawTiles();

        // Draws the visible tiles of one level in a style and asks for the missing ones; with fallback, a missing
        // tile shows a cached coarser one scaled.
        void drawLevel(int level, const BitmapStyle& style, bool fallback);

        // Draws a cached tile, and asks for a fresh one when the selection changed under it.
        void drawCached(core::TileId tile, const GpuTileHandle& cached, const PixelRect& destination,
                        const BitmapStyle& style);

        // Draws the nearest cached coarser tile scaled into a missing tile's place; false when none is cached.
        bool drawFallback(core::TileId tile, const PixelRect& destination);

        // Queues a tile on the pool unless it is cached, already rendering, or too many are in flight.
        void request(core::TileId tile);

        // Forgets all tiles (new map, lost device); late results are dropped by generation.
        void resetTiles();

        // Draws the lines between cells at deep zoom, at screen resolution and faded in by cell size.
        void drawGrid();

        // Draws the minimap, asking for its image (the coarsest tile) until it has one.
        void drawMinimap();

        // Places the minimap for the current map and window size.
        void layoutMinimap();

        // Tells the observer that the view or the map changed.
        void notifyView() const;

        // Uploads up to a per-frame budget of arrived tiles; repaints again when more are waiting.
        void uploadArrived();

        // Puts an uploaded tile in the cache (and the minimap for the top tile); clears its dirty mark when it
        // was drawn with a selection at least as new as the mark.
        void storeTile(core::TileId tile, std::uint64_t selection, GpuTileHandle uploaded);

        // A rendered tile waiting for upload.
        struct ArrivedTile {
            core::TileId tile; // Which tile.
            std::uint64_t selection = 0; // Selection version it was drawn with.
            std::vector<core::Rgba> pixels; // Its pixels.
        };

        core::Executor& executor_; // Renders tiles.
        std::unique_ptr<D2DRenderer> renderer_; // Created on first paint.
        std::unique_ptr<MapNavigator> navigator_; // Input and animation.
        std::unique_ptr<MapSelection> selection_; // Selected file; sees clicks after the hover, before the navigator.
        std::unique_ptr<MapHover> hover_; // Tooltip; created after the navigator so it sees events first.
        core::LruCache<core::TileId, GpuTileHandle, core::TileIdHash> cache_{tileCacheBytes}; // Uploaded tiles.
        std::unordered_set<core::TileId, core::TileIdHash> pending_; // Tiles rendering now.
        std::deque<ArrivedTile> arrived_; // Rendered, not yet uploaded (uploads are spread over frames).
        std::shared_ptr<TileMailbox> mailbox_; // Route from workers back to this window.
        std::shared_ptr<const core::MapData> map_; // What is shown; null for no map.
        std::shared_ptr<const core::Palette> palette_; // Color mode.
        std::shared_ptr<const core::SelectionOverlay> overlay_; // Highlighted file, or null.
        std::shared_ptr<const core::MapOverlay> mapOverlay_; // Plan preview or zone fit painted under the selection.
        // Tiles whose cached (or rendering) pixels show an older selection or palette, with the version that made
        // them stale.
        std::unordered_map<core::TileId, std::uint64_t, core::TileIdHash> dirty_;
        std::uint64_t selectionVersion_ = 0; // Content version, bumped on every selection or palette change.
        core::Viewport view_; // Current view.
        Minimap minimap_; // Overview in the corner.
        std::function<void()> viewObserver_; // Called when the view or map changes.
        std::uint64_t generation_ = 0; // Bumped whenever cached tiles become invalid.
        bool fitted_ = false; // Whether the current map was fitted to a real window size yet.
        std::vector<double> frameIntervals_; // Milliseconds between frames while animating.
        std::vector<double> paintTimes_; // Milliseconds spent painting each of those frames.
        std::optional<core::AnimationClock::time_point> lastFrame_; // Start of the previous animated frame.
        bool recording_ = false; // Whether frame times are being recorded.
        std::size_t missingTiles_ = 0; // Exact-level tiles missing from the last frame.
    };

} // namespace icd::ui
