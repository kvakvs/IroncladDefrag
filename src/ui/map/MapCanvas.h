#pragma once

#include "core/map/MapData.h"
#include "core/map/Palette.h"
#include "core/util/LruCache.h"
#include "ui/map/D2DRenderer.h"

#include <wx/window.h>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace icd::core {
    class Executor;
}

namespace icd::ui {

    struct TileMailbox;

    // The drive map widget (plan.md 7.3): composes 256 x 256 tiles with Direct2D, renders missing tiles on the
    // shared pool and keeps the uploaded ones in an LRU cache with a memory cap. The paint path never touches
    // analysis data: it only draws cached tiles and asks for missing ones. The world is fixed per map, so
    // resizing the window changes how much is visible, never the layout (plan.md 7.1).
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

        // Zooms and centers so the whole volume is visible.
        void fitToView();

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const { return map_ != nullptr; }

        // Takes a rendered tile on the UI thread (called through the mailbox).
        void deliver(std::uint64_t generation, core::TileId tile, const std::vector<core::Rgba>& pixels);

    private:
        // What part of the world is shown.
        struct View {
            double scale = 1.0; // World cells per window pixel.
            double centerX = 0.0; // World cell at the window's center.
            double centerY = 0.0; // World cell at the window's center.
        };

        // Draws the frame.
        void onPaint(wxPaintEvent& event);

        // Fits the first time the window gets a real size; afterwards keeps the view (no re-flow).
        void onSize(wxSizeEvent& event);

        // Draws the visible tiles of the current level and asks for the missing ones.
        void drawTiles(wxSize size);

        // Queues a tile on the pool unless it is cached or already rendering.
        void request(core::TileId tile);

        // Forgets all tiles (new map, new palette, lost device); late results are dropped by generation.
        void resetTiles();

        core::Executor& executor_; // Renders tiles.
        std::unique_ptr<D2DRenderer> renderer_; // Created on first paint.
        core::LruCache<core::TileId, GpuTileHandle, core::TileIdHash> cache_{tileCacheBytes}; // Uploaded tiles.
        std::unordered_set<core::TileId, core::TileIdHash> pending_; // Tiles rendering now.
        std::shared_ptr<TileMailbox> mailbox_; // Route from workers back to this window.
        std::shared_ptr<const core::MapData> map_; // What is shown; null for no map.
        std::shared_ptr<const core::Palette> palette_; // Color mode.
        View view_; // Current view.
        std::uint64_t generation_ = 0; // Bumped whenever cached tiles become invalid.
        bool fitted_ = false; // Whether the current map was fitted to a real window size yet.
    };

} // namespace icd::ui
