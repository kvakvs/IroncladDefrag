#pragma once

#include "core/map/Palette.h"
#include "core/map/World.h"
#include "core/planner/Plan.h"

#include <memory>
#include <span>
#include <vector>

namespace icd::core {

    // Runs painted in one color over rendered tiles.
    struct RunLayer {
        std::vector<ClusterRun> runs{}; // Sorted by LCN, disjoint, not touching.
        Rgba color = 0; // Their color.
    };

    // What the plan-preview and zone-fit map modes draw over the base map (plan.md 7.4): layers painted in order, so
    // later layers cover earlier ones. Immutable and shared with the tile workers, like the selection.
    struct MapOverlay {
        std::vector<RunLayer> layers{}; // Bottom first.
    };

    // Colors of the plan preview: sources hollow (a pale tint: the data leaves), destinations solid.
    inline constexpr Rgba planSourceColor = rgb(250, 200, 150);
    inline constexpr Rgba planDestinationColor = rgb(30, 90, 220);

    // Sorts runs and merges overlapping and touching ones.
    [[nodiscard]] std::vector<ClusterRun> mergedRuns(std::vector<ClusterRun> runs);

    // The plan preview of a plan: every move's sources, then every destination over them (a cluster that is both, as
    // in a chain of moves, shows as a destination).
    [[nodiscard]] std::shared_ptr<const MapOverlay> planOverlay(const planner::Plan& plan);

    // Paints an overlay into a rendered tile: every pixel holding a cluster of a layer's runs takes its color.
    void paintOverlay(const World& world, TileId tile, const MapOverlay& overlay, std::span<Rgba> pixels);

} // namespace icd::core
