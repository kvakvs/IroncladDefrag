#include "core/map/PlanColorMode.h"

#include "core/map/Selection.h"

#include <algorithm>

namespace icd::core {

    std::vector<ClusterRun> mergedRuns(std::vector<ClusterRun> runs) {
        std::erase_if(runs, [](const ClusterRun& r) { return r.length == 0; });
        std::ranges::sort(runs, {}, &ClusterRun::start);
        std::vector<ClusterRun> out;
        for (const ClusterRun& r : runs) {
            if (!out.empty() && r.start <= out.back().end()) {
                out.back().length = std::max(out.back().end(), r.end()) - out.back().start;
            }
            else {
                out.push_back(r);
            }
        }
        return out;
    }

    std::shared_ptr<const MapOverlay> planOverlay(const planner::Plan& plan) {
        std::vector<ClusterRun> sources;
        std::vector<ClusterRun> destinations;
        for (const planner::Move& m : plan.moves) {
            const auto from = plan.sources(m);
            const auto to = plan.destinations(m);
            sources.insert(sources.end(), from.begin(), from.end());
            destinations.insert(destinations.end(), to.begin(), to.end());
        }
        auto overlay = std::make_shared<MapOverlay>();
        overlay->layers.push_back({.runs = mergedRuns(std::move(sources)), .color = planSourceColor});
        overlay->layers.push_back({.runs = mergedRuns(std::move(destinations)), .color = planDestinationColor});
        return overlay;
    }

    void paintOverlay(const World& world, TileId tile, const MapOverlay& overlay, std::span<Rgba> pixels) {
        for (const RunLayer& layer : overlay.layers) {
            highlightRuns(world, tile, layer.runs, layer.color, pixels);
        }
    }

} // namespace icd::core
