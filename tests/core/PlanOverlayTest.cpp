// Plan preview and zone fit on the map (T098): at one pixel per cluster every cluster a plan moves into shows as a
// destination, every cluster it vacates (and nothing moves into) as a source, and nothing else; the zone-fit overlay
// marks exactly the clusters of placed files outside their zone.

#include "core/map/PlanColorMode.h"
#include "core/map/TileRasterizer.h"
#include "core/map/ZoneFitColorMode.h"
#include "core/planner/levels/MaximumLevel.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core { namespace {

    // A small synthetic volume and its Maximum plan under the mixed preset.
    struct Planned {
        Snapshot snapshot;
        std::vector<classify::ClassId> classes;
        planner::ZoneAssignment assignment;
        planner::PlanResult result;
    };

    // Plans a small synthetic volume.
    Planned plannedVolume() {
        SynthOptions options;
        options.seed = 91;
        options.files = 400;
        options.geometry.totalClusters = 1ULL << 15U;
        options.fragmentedShare = 0.4;
        Planned p;
        p.snapshot = generateSyntheticVolume(options);
        p.classes = planner::test::classesOf(p.snapshot);
        const auto& script = strategy::builtinPresets().front();
        const planner::PlanInput input{.snapshot = &p.snapshot,
                                       .script = &script,
                                       .catalog = &classify::builtinCatalog(),
                                       .classes = p.classes,
                                       .churn = {},
                                       .now = planner::test::now};
        InlineExecutor serial;
        p.assignment = planner::assignInput(input, serial);
        p.result = planner::planMaximum(input, p.assignment, serial);
        return p;
    }

    // Per cluster: whether one of the runs holds it.
    std::vector<bool> flags(ClusterCount clusters, std::span<const ClusterRun> runs) {
        std::vector<bool> out(clusters, false);
        for (const ClusterRun& r : runs) {
            for (Lcn l = r.start; l < r.end(); ++l) {
                out[l] = true;
            }
        }
        return out;
    }

    // Checks every pixel of every level-0 tile (one cluster per pixel) against per-cluster expectations.
    void checkPixels(const World& world, const MapOverlay& overlay, const std::vector<Rgba>& expected) {
        constexpr Rgba base = rgb(1, 2, 3);
        for (std::uint64_t ty = 0; ty < world.tilesDown(0); ++ty) {
            for (std::uint64_t tx = 0; tx < world.tilesAcross(0); ++tx) {
                const TileId tile{0, tx, ty};
                std::vector<Rgba> pixels(tilePixels, base);
                paintOverlay(world, tile, overlay, pixels);
                const CellRect rect = world.tileCells(tile);
                for (std::uint64_t py = 0; py < World::tileSize; ++py) {
                    for (std::uint64_t px = 0; px < World::tileSize; ++px) {
                        const auto lcn = world.lcnAt({rect.x + px, rect.y + py});
                        const Rgba want = lcn ? expected[*lcn] : base;
                        ASSERT_EQ(pixels[py * World::tileSize + px], want) << tx << "," << ty << " " << px << "," << py;
                    }
                }
            }
        }
    }

    TEST(PlanOverlay, PreviewMatchesThePlansMoves) {
        const Planned p = plannedVolume();
        const auto& plan = p.result.plan;
        ASSERT_GT(plan.moves.size(), 50U);
        std::vector<ClusterRun> sources;
        std::vector<ClusterRun> destinations;
        for (const auto& m : plan.moves) {
            sources.insert(sources.end(), plan.sources(m).begin(), plan.sources(m).end());
            destinations.insert(destinations.end(), plan.destinations(m).begin(), plan.destinations(m).end());
        }
        const ClusterCount clusters = p.snapshot.geometry.totalClusters;
        const auto from = flags(clusters, sources);
        const auto to = flags(clusters, destinations);
        std::vector<Rgba> expected(clusters, rgb(1, 2, 3));
        for (Lcn l = 0; l < clusters; ++l) {
            expected[l] = to[l] ? planDestinationColor : (from[l] ? planSourceColor : expected[l]);
        }
        const World world(clusters, {.width = 16, .height = 9});
        checkPixels(world, *planOverlay(plan), expected);
    }

    TEST(PlanOverlay, ZoneFitMarksMisplacedData) {
        const Planned p = plannedVolume();
        const auto& layout = p.result.layout;
        const ClusterCount clusters = p.snapshot.geometry.totalClusters;
        std::vector<ClusterRun> targets;
        for (const auto& t : layout.zones) {
            targets.insert(targets.end(), t.pieces.begin(), t.pieces.end());
        }
        const auto inTarget = flags(clusters, targets);
        std::vector<Rgba> expected(clusters, rgb(1, 2, 3));
        std::size_t misplaced = 0;
        for (Lcn l = 0; l < clusters; ++l) {
            expected[l] = inTarget[l] ? zoneTargetColor : expected[l];
        }
        for (std::size_t f = 0; f < p.assignment.roles.size(); ++f) {
            const auto* t = p.assignment.roles[f] == planner::FileRole::Placed ? layout.target(p.assignment.zoneOf[f])
                                                                                : nullptr;
            for (const ClusterRun& r : t != nullptr ? planner::movableRuns(p.snapshot, static_cast<FileIndex>(f))
                                                    : std::vector<ClusterRun>{}) {
                for (Lcn l = r.start; l < r.end() && !t->holds(r); ++l) {
                    expected[l] = misplacedColor;
                    ++misplaced;
                }
            }
        }
        EXPECT_GT(misplaced, 1000U);
        const World world(clusters, {.width = 16, .height = 9});
        checkPixels(world, *zoneFitOverlay(p.snapshot, p.assignment, layout), expected);
    }

}} // namespace icd::core
