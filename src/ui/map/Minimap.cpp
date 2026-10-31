#include "ui/map/Minimap.h"

#include <algorithm>
#include <cmath>

namespace icd::ui {

    namespace {

        // Border of the minimap and outer line of the view marker: dark, so both stand out on every map color.
        constexpr core::Rgba frameColor = core::rgb(40, 40, 40);

        // Inner line of the view marker.
        constexpr core::Rgba markerColor = core::rgb(255, 200, 0);

        // Where there is no image yet.
        constexpr core::Rgba emptyColor = core::rgb(200, 200, 200);

        // Longest side of the minimap and its gap to the window corner, in DIPs.
        constexpr double maxEdgeDip = 200.0;
        constexpr double marginDip = 12.0;

        // A renderer rectangle from an overview box.
        PixelRect rectOf(const core::PixelBox& box) { return {box.x, box.y, box.width, box.height}; }

        // A box grown by margin on every side.
        PixelRect grown(const core::PixelBox& box, double margin) {
            return {box.x - margin, box.y - margin, box.width + 2.0 * margin, box.height + 2.0 * margin};
        }

    } // namespace

    void Minimap::layout(const core::World* world, core::PixelPoint size, double dipScale) {
        core::OverviewStyle style;
        style.maxEdge = maxEdgeDip * dipScale;
        style.margin = marginDip * dipScale;
        overview_ = world != nullptr && shown_ ? core::placeOverview(*world, size, style) : std::nullopt;
        const double unit = std::max(1.0, std::round(dipScale)); // Whole pixels keep lines sharp.
        markerMinEdge_ = 6.0 * unit;
        frameWidth_ = unit;
        if (!overview_) {
            grab_.reset();
        }
    }

    void Minimap::draw(D2DRenderer& renderer, const core::Viewport& view, const core::World& world) const {
        if (!overview_) {
            return;
        }
        const core::PixelBox& box = overview_->box;
        renderer.fillRect(grown(box, frameWidth_), frameColor);
        if (image_ != nullptr) {
            // At the coarsest level one tile pixel is 2^maxLevel cells; the world fills the tile's top-left part.
            const double cells = core::World::cellsPerPixel(world.maxLevel());
            const PixelRect source{0, 0, static_cast<double>(world.rowWidth()) / cells,
                                   static_cast<double>(world.rows()) / cells};
            renderer.draw(image_, rectOf(box), source, true);
        }
        else {
            renderer.fillRect(rectOf(box), emptyColor);
        }
        const core::PixelBox marker = overview_->viewBox(view, markerMinEdge_);
        renderer.strokeRect(rectOf(marker), {.color = frameColor, .width = static_cast<float>(3.0 * frameWidth_)});
        renderer.strokeRect(rectOf(marker), {.color = markerColor, .width = static_cast<float>(frameWidth_)});
    }

    bool Minimap::contains(core::PixelPoint pixel) const { return overview_ && overview_->box.contains(pixel); }

    bool Minimap::beginDrag(core::PixelPoint pointer, const core::Viewport& view) {
        if (!overview_ || !overview_->box.contains(pointer)) {
            return false;
        }
        // Holding the marker keeps the offset; a click elsewhere centers the view under the pointer.
        if (overview_->viewBox(view, markerMinEdge_).contains(pointer)) {
            const core::PixelPoint center = overview_->pixelAt(view.center);
            grab_ = core::PixelPoint{pointer.x - center.x, pointer.y - center.y};
        }
        else {
            grab_ = core::PixelPoint{};
        }
        return true;
    }

    core::Viewport Minimap::dragTo(core::PixelPoint pointer, const core::Viewport& view, const core::World& world,
                                   const core::ViewLimits& limits) const {
        if (!overview_ || !grab_) {
            return view;
        }
        return overview_->dragTo(view, pointer, *grab_, world, limits);
    }

} // namespace icd::ui
