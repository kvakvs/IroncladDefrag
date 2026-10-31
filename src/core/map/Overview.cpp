#include "core/map/Overview.h"

#include <algorithm>

namespace icd::core {

    namespace {

        // Clips the span [from, from + length) to [low, high), then grows it around its middle to at least
        // minLength, kept inside [low, high).
        void fitSpan(double& from, double& length, double low, double high, double minLength) {
            const double start = std::clamp(from, low, high);
            const double end = std::clamp(from + length, low, high);
            from = start;
            length = end - start;
            if (length >= minLength) {
                return;
            }
            const double grown = std::min(minLength, high - low);
            const double middle = (start + end) / 2.0;
            from = std::clamp(middle - grown / 2.0, low, high - grown);
            length = grown;
        }

    } // namespace

    WorldPoint Overview::worldAt(PixelPoint pixel, const World& world) const {
        const double x = (pixel.x - box.x) * scale;
        const double y = (pixel.y - box.y) * scale;
        return {std::clamp(x, 0.0, static_cast<double>(world.rowWidth())),
                std::clamp(y, 0.0, static_cast<double>(world.rows()))};
    }

    PixelPoint Overview::pixelAt(WorldPoint point) const { return {box.x + point.x / scale, box.y + point.y / scale}; }

    PixelBox Overview::viewBox(const Viewport& view, double minEdge) const {
        const PixelPoint topLeft = pixelAt(view.worldAt({0, 0}));
        const PixelPoint bottomRight = pixelAt(view.worldAt(view.size));
        PixelBox marked{topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
        fitSpan(marked.x, marked.width, box.x, box.x + box.width, minEdge);
        fitSpan(marked.y, marked.height, box.y, box.y + box.height, minEdge);
        return marked;
    }

    Viewport Overview::dragTo(const Viewport& view, PixelPoint pointer, PixelPoint grab, const World& world,
                              const ViewLimits& limits) const {
        Viewport moved = view;
        moved.center = worldAt({pointer.x - grab.x, pointer.y - grab.y}, world);
        return clampView(moved, limits);
    }

    std::optional<Overview> placeOverview(const World& world, PixelPoint size, const OverviewStyle& style) {
        const auto width = static_cast<double>(world.rowWidth());
        const auto height = static_cast<double>(world.rows());
        if (world.clusters() == 0 || style.maxEdge <= 0.0) {
            return std::nullopt;
        }
        Overview overview;
        overview.scale = std::max(width, height) / style.maxEdge;
        overview.box.width = width / overview.scale;
        overview.box.height = height / overview.scale;
        const bool fits =
            size.x >= overview.box.width * style.minWindowShare && size.y >= overview.box.height * style.minWindowShare;
        if (!fits) {
            return std::nullopt;
        }
        overview.box.x = size.x - style.margin - overview.box.width;
        overview.box.y = size.y - style.margin - overview.box.height;
        return overview;
    }

} // namespace icd::core
