#pragma once

#include "core/map/Palette.h"

#include <wx/defs.h>
#include <wx/gdicmn.h>

#include <memory>
#include <vector>

namespace icd::ui {

    // A tile uploaded to the GPU; opaque so Direct2D stays inside the renderer.
    struct GpuTile;

    // Shared handle to an uploaded tile (the tile cache and a frame in progress may both hold it).
    using GpuTileHandle = std::shared_ptr<GpuTile>;

    // A destination or source rectangle in pixels.
    struct PixelRect {
        double x = 0; // Left.
        double y = 0; // Top.
        double width = 0; // Width.
        double height = 0; // Height.
    };

    // How an outline is drawn.
    struct LineStyle {
        core::Rgba color = 0xFF000000U; // Line color.
        float width = 1.0F; // Line width in pixels.
    };

    // How a tile is drawn.
    struct BitmapStyle {
        bool smooth = false; // Linear filtering; otherwise nearest-neighbour, which keeps large cells crisp.
        double opacity = 1.0; // 0-1: how strongly it covers what is drawn below (fading between map levels).
    };

    // How a set of rectangles is filled.
    struct FillStyle {
        core::Rgba color = 0xFF000000U; // Fill color.
        double opacity = 1.0; // 0-1: how strongly the fill covers what is drawn below.
    };

    // Direct2D drawing on one window, the map backend chosen by the T026 spike: one HWND render target, tiles
    // as GPU bitmaps, nearest-neighbour scaling. Units are physical pixels. Used on the UI thread only.
    class D2DRenderer {
    public:
        // Draws into window.
        explicit D2DRenderer(WXHWND window);

        // Releases the target and the factory.
        ~D2DRenderer();

        D2DRenderer(const D2DRenderer&) = delete;
        D2DRenderer& operator=(const D2DRenderer&) = delete;

        // Starts a frame of the given size (creating or resizing the target); false when no target exists.
        bool begin(wxSize size);

        // Fills the frame with a color.
        void clear(core::Rgba color);

        // Uploads a 256 x 256 tile; null when no target exists.
        [[nodiscard]] GpuTileHandle upload(const std::vector<core::Rgba>& pixels);

        // Draws source (in tile pixels) of a tile into destination (in window pixels). Nearest-neighbour keeps
        // large cells crisp; smooth (linear) suits tiles of cells under a pixel and small overviews (the minimap).
        // Edges are not anti-aliased, so tiles that meet at a fractional pixel leave no seam.
        void draw(const GpuTileHandle& tile, const PixelRect& destination, const PixelRect& source,
                  const BitmapStyle& style = {});

        // Fills a rectangle with a color; overlays on the map.
        void fillRect(const PixelRect& rect, core::Rgba color) { fillRect(rect, {.color = color, .opacity = 1.0}); }

        // Fills a rectangle with a color at the style's opacity, through the brush (no layer): the activity pulses.
        void fillRect(const PixelRect& rect, const FillStyle& fill);

        // Fills rectangles with one color, blended over what is drawn at the style's opacity as one layer, so
        // where rectangles overlap (grid line crossings) the result is no brighter than elsewhere.
        void fillRects(const std::vector<PixelRect>& rects, const FillStyle& fill);

        // Outlines a rectangle with a line centered on its edges.
        void strokeRect(const PixelRect& rect, const LineStyle& line);

        // Draws what follows until popScaledClip() clipped to `clip` and scaled by scaleX across and scaleY down about
        // its center, clip and all: a read or write pulse fills its bar of the map this way (T114).
        void pushScaledClip(const PixelRect& clip, double scaleX, double scaleY);

        // Ends what pushScaledClip() started.
        void popScaledClip();

        // Ends and presents the frame. False when the device was lost: every uploaded tile is then invalid and
        // must be dropped; the next begin() creates a new target.
        bool end();

    private:
        struct Impl; // Direct2D objects.
        std::unique_ptr<Impl> impl_; // Owned Direct2D state.
    };

} // namespace icd::ui
