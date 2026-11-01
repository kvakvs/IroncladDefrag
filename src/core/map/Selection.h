#pragma once

#include "core/map/Palette.h"
#include "core/map/Viewport.h"
#include "core/map/World.h"
#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/Types.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace icd::core {

    // The fragments of a file in VCN order, each as one run: records that continue exactly where the previous one
    // ended (split long runs) join, as in ExtentIndex::fragmentCount(), so fragment i here is fragment i/n of the
    // hover tooltip. Empty for files without clusters.
    [[nodiscard]] std::vector<ClusterRun> fileFragments(const ExtentIndex& extents, FileIndex file);

    // What the map highlights for a selected file (plan.md 7.4): its clusters as runs sorted by LCN, with touching
    // runs merged. Shared, immutable data that tile workers read while the UI may already select another file.
    struct SelectionOverlay {
        FileIndex file = noFile; // The selected file.
        std::vector<ClusterRun> runs; // Its clusters, sorted by LCN, disjoint and not touching.
    };

    // The overlay of a file.
    [[nodiscard]] SelectionOverlay selectionOf(const ExtentIndex& extents, FileIndex file);

    // A run as the cell rectangles it covers: a run wraps at row ends, so it is at most a partial first row, a block
    // of full rows and a partial last row. Lets drawing and tile tests work on whole rectangles instead of on every
    // row of a long run.
    struct RunShape {
        std::array<CellRect, 3> rects{}; // The rectangles, top to bottom.
        std::size_t count = 0; // How many of rects are used.
    };

    // The rectangles of a run (none for an empty run).
    [[nodiscard]] RunShape shapeOf(const World& world, ClusterRun run);

    // Whether any run (sorted by LCN, disjoint) has a cell inside rect. The canvas asks this for every cached tile
    // when the selection changes, so only the tiles whose pixels change are rendered again.
    [[nodiscard]] bool runsTouch(const World& world, std::span<const ClusterRun> runs, CellRect rect);

    // Paints the selection into a rendered tile: every pixel holding at least one cluster of runs (sorted by LCN,
    // disjoint) gets color, so even a one-cluster fragment shows at the widest zoom.
    void highlightRuns(const World& world, TileId tile, std::span<const ClusterRun> runs, Rgba color,
                       std::span<Rgba> pixels);

    // The view that shows a run (a fragment): centered on its cells, zoomed so they fill about half the window
    // (a run that wraps shows its whole rows across most of the width), but no closer than 8 px per cluster;
    // within limits.
    [[nodiscard]] Viewport focusView(const World& world, const Viewport& view, ClusterRun run,
                                     const ViewLimits& limits);

} // namespace icd::core
