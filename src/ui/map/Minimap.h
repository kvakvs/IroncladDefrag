#pragma once

#include "core/map/Overview.h"
#include "ui/map/D2DRenderer.h"

#include <optional>
#include <utility>

namespace icd::ui {

    // The minimap overlay of the drive map (plan.md 7.4): the whole volume in the bottom-right corner with the
    // visible part marked, dragged to pan. Drawn inside the map's own Direct2D frame (no child window over the
    // GPU surface); the image is the coarsest tile, held here so the tile cache cannot evict it. The math is
    // core::Overview; this class adds drawing and the drag state. UI thread only.
    class Minimap {
    public:
        // Sizes and places the minimap for a world in a window of size pixels (null world hides it); dipScale is
        // pixels per DIP on the window's screen.
        void layout(const core::World* world, core::PixelPoint size, double dipScale);

        // Shows or hides the minimap (View > Minimap); takes effect at the next layout().
        void setShown(bool shown) { shown_ = shown; }

        // Whether the user wants the minimap.
        [[nodiscard]] bool shown() const { return shown_; }

        // Drops the image (new map, color mode or lost device); the canvas supplies a new one.
        void resetImage() { image_.reset(); }

        // Whether it is shown and still waits for its image.
        [[nodiscard]] bool needsImage() const { return overview_.has_value() && image_ == nullptr; }

        // Takes the image: the world's coarsest tile.
        void setImage(GpuTileHandle image) { image_ = std::move(image); }

        // Draws the frame, the volume and the view marker for a view of a world.
        void draw(D2DRenderer& renderer, const core::Viewport& view, const core::World& world) const;

        // Whether a window pixel is on the minimap.
        [[nodiscard]] bool contains(core::PixelPoint pixel) const;

        // Takes hold at a pointer for dragging; false (and nothing changes) when the pointer is not on the minimap.
        bool beginDrag(core::PixelPoint pointer, const core::Viewport& view);

        // The view while dragging with the pointer at a pixel.
        [[nodiscard]] core::Viewport dragTo(core::PixelPoint pointer, const core::Viewport& view,
                                            const core::World& world, const core::ViewLimits& limits) const;

        // Lets go.
        void endDrag() { grab_.reset(); }

        // Whether a drag is in progress.
        [[nodiscard]] bool dragging() const { return grab_.has_value(); }

    private:
        std::optional<core::Overview> overview_; // Placement; nullopt while hidden.
        GpuTileHandle image_; // The coarsest tile, or null until it is rendered.
        std::optional<core::PixelPoint> grab_; // While dragging: pointer offset from the view center's pixel.
        double markerMinEdge_ = 6.0; // Smallest view marker, so it stays visible at deep zoom.
        double frameWidth_ = 1.0; // Border around the minimap.
        bool shown_ = true; // View > Minimap.
    };

} // namespace icd::ui
