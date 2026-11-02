// Fast-fill (T085): holes at a zone's head filled best-fit from its tail; bytes moved equal the lower bound on test
// layouts; destinations stay in the zone, never overlap each other or the file's own runs, and use only free or vacated
// space.

#include "core/planner/CandidateFilter.h"
#include "core/planner/FastFill.h"
#include "core/planner/Validator.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <set>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::ZoneAction;

    // Everything a fill test needs from one volume and script.
    struct FillCase {
        ZoneAssignment assignment;
        TargetLayout layout;
        LowerBound bound;
        Candidates candidates;
        HoleSet pool; // Free after the plan: free runs plus every candidate's runs.
    };

    // Assigns, fits, resolves and bounds a snapshot under a script.
    FillCase setUp(const Snapshot& snapshot, std::span<const classify::ClassId> classes,
                   const strategy::ZoneScript& s) {
        InlineExecutor serial;
        FillCase out;
        out.assignment = test::assignSnapshot(snapshot, classes, s);
        const auto in = fitTarget(snapshot, s, out.assignment, classes, classify::builtinCatalog(), {}, {}, serial);
        out.layout = resolveLayout(snapshot, s, out.assignment, classes, classify::builtinCatalog(), in);
        out.bound = lowerBound(snapshot, s, out.assignment, out.layout, {}, serial);
        out.candidates = filterCandidates(out.bound, out.assignment, s.zones.size());
        out.pool = HoleSet(snapshot.freeSpace.runs());
        for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
            if (out.bound.mustMove[f] != 0) {
                releaseRuns(out.pool, movableRuns(snapshot, static_cast<FileIndex>(f)));
            }
        }
        return out;
    }

    // Fast-fills every placed zone of a setup in script order.
    std::vector<Destination> fillAll(const Snapshot& snapshot, FillCase& setup,
                                     std::vector<FileIndex>* overflow = nullptr) {
        std::vector<Destination> all;
        for (const ZoneTarget& t : setup.layout.zones) {
            const FillOptions options{.from = t.back ? PackFrom::Back : PackFrom::Front,
                                      .zone = t.zone,
                                      .home = {t.firstLcn().value_or(0), t.endLcn() - t.firstLcn().value_or(0)}};
            auto result = fastFill(snapshot, t.pieces, setup.candidates.perZone[t.zone], setup.pool, options);
            all.insert(all.end(), result.placed.begin(), result.placed.end());
            if (overflow != nullptr) {
                overflow->insert(overflow->end(), result.overflow.begin(), result.overflow.end());
            }
        }
        return all;
    }

    // Static checks of destinations: inside their zone, disjoint, off the file's own runs, only free or vacated
    // clusters, cluster counts kept. Returns the first problem, or an empty string.
    std::string checkDestinations(const Snapshot& snapshot, const FillCase& setup,
                                  const std::vector<Destination>& all) {
        HoleSet space(snapshot.freeSpace.runs());
        for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
            if (setup.bound.mustMove[f] != 0) {
                releaseRuns(space, movableRuns(snapshot, static_cast<FileIndex>(f)));
            }
        }
        for (const Destination& d : all) {
            const auto own = sortedRuns(movableRuns(snapshot, d.file));
            if (clustersOf(d.runs) != clustersOf(own)) {
                return std::format("file {} changes its cluster count", d.file);
            }
            for (const ClusterRun& r : d.runs) {
                const ZoneTarget* t = setup.layout.target(d.zone);
                if (t == nullptr || !t->holds(r)) {
                    return std::format("file {} lands outside its zone", d.file);
                }
                if (std::ranges::any_of(own,
                                        [&](const ClusterRun& o) { return o.start < r.end() && r.start < o.end(); })) {
                    return std::format("file {} lands on its own clusters", d.file);
                }
                if (!space.take(r)) {
                    return std::format("file {} lands on space that is not free after the plan (or twice)", d.file);
                }
            }
        }
        return {};
    }

    // Movable clusters of a destination list.
    ClusterCount placedClusters(const std::vector<Destination>& all) {
        ClusterCount sum = 0;
        for (const Destination& d : all) {
            sum += clustersOf(d.runs);
        }
        return sum;
    }

    // Letters A, C, D, E are photos, B archives.
    const test::PictureFacts photoFacts{
        .classes = {{'A', "photos"}, {'C', "photos"}, {'D', "photos"}, {'E', "photos"}, {'B', "archives"}}};

    // Photos packed at the front, archives after them.
    strategy::ZoneScript photosThenArchives() {
        return test::script({test::zone("Photos", {"photos"}, ZoneAction::FastFill, Anchor::Front),
                             test::zone("Archives", {"archives"}, ZoneAction::FastFill)});
    }

    TEST(FastFill, TailFilesFillHeadHolesAndBytesEqualTheLowerBound) {
        //  0 1 2 3 4 5 6 7 8 9 10 11
        //  A . C C . . D . E . B  B      photos -> [0,5); archives shift to [10,12), where B already is
        const auto v = test::pictureVolume("A.CC..D.E.BB", photoFacts);
        FillCase setup = setUp(v.snapshot, v.classes, photosThenArchives());
        const auto all = fillAll(v.snapshot, setup);
        EXPECT_EQ(checkDestinations(v.snapshot, setup, all), "");
        EXPECT_EQ(placedClusters(all), setup.bound.clusters);
        EXPECT_EQ(setup.bound.clusters, 2U); // D and E.
        Plan plan;
        plan.lowerBound = setup.bound.clusters;
        appendMoves(v.snapshot, all, plan);
        finishTotals(plan, v.snapshot);
        const Validation result = validatePlan(v.snapshot, plan, {.layout = &setup.layout});
        EXPECT_TRUE(result.ok()) << result.issues.front().message;
        ASSERT_EQ(plan.moves.size(), 2U);
        EXPECT_EQ(plan.moves[0].reason, MoveReason::Relocate);
        EXPECT_EQ(plan.destinations(plan.moves[0])[0], (ClusterRun{1, 1}));
    }

    TEST(FastFill, HolesTakeTheLargestFileThatFits) {
        //  0 1 2 3 4 5 6 7 8 9 10
        //  . . . Z . . X X Y Y Y      zone [0,6): Y fills [0,3), X fills [4,6)
        const auto v = test::pictureVolume("...Z..XXYYY");
        FillCase setup =
            setUp(v.snapshot, v.classes, test::script({test::zone("All", {}, ZoneAction::FastFill, Anchor::Front)}));
        const auto all = fillAll(v.snapshot, setup);
        ASSERT_EQ(all.size(), 2U);
        EXPECT_EQ(all[0].file, v.files.at('Y'));
        EXPECT_EQ(all[0].runs, (std::vector<ClusterRun>{{0, 3}}));
        EXPECT_EQ(all[1].runs, (std::vector<ClusterRun>{{4, 2}}));
        EXPECT_EQ(placedClusters(all), setup.bound.clusters);
    }

    TEST(FastFill, BackPackingFillsFromTheEnd) {
        //  V V . W . . . . .  video packed against the end: [6,9)
        const test::PictureFacts facts{.classes = {{'V', "video"}, {'W', "video"}}};
        const auto v = test::pictureVolume("VV.W.....", facts);
        const auto s = test::script({test::zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back)});
        FillCase setup = setUp(v.snapshot, v.classes, s);
        const auto all = fillAll(v.snapshot, setup);
        ASSERT_EQ(all.size(), 2U);
        EXPECT_EQ(all[0].runs, (std::vector<ClusterRun>{{7, 2}})); // The larger file takes the last hole's end.
        EXPECT_EQ(all[1].runs, (std::vector<ClusterRun>{{6, 1}}));
        EXPECT_EQ(checkDestinations(v.snapshot, setup, all), "");
    }

    TEST(FastFill, NeverLandsOnTheFilesOwnClusters) {
        // A's zone is [0,2); the only room there is its own cluster 0 plus cluster 1: it cannot move onto itself.
        const auto v = test::pictureVolume("A.A..", {.classes = {{'A', "photos"}}});
        FillCase setup = setUp(v.snapshot, v.classes, photosThenArchives());
        std::vector<FileIndex> overflow;
        const auto all = fillAll(v.snapshot, setup, &overflow);
        EXPECT_TRUE(all.empty());
        EXPECT_EQ(overflow, (std::vector<FileIndex>{v.files.at('A')}));
    }

    TEST(FastFill, SyntheticVolumeFillsWithinTheZoneUpToTheLowerBound) {
        SynthOptions options;
        options.seed = 9;
        options.files = 2000;
        options.geometry.totalClusters = 1ULL << 19U;
        options.fillPercent = 60;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const auto classes = test::classesOf(snapshot);
        FillCase setup = setUp(snapshot, classes, test::script({test::zone("All", {}, ZoneAction::FastFill)}));
        std::vector<FileIndex> overflow;
        const auto all = fillAll(snapshot, setup, &overflow);
        EXPECT_EQ(checkDestinations(snapshot, setup, all), "");
        ClusterCount overflowClusters = 0;
        for (const FileIndex f : overflow) {
            overflowClusters += setup.assignment.movable[f];
        }
        EXPECT_EQ(placedClusters(all) + overflowClusters, setup.bound.clusters);
        // The zone is exact and its holes are the volume's scattered small gaps, so large fragmented files find no
        // single hole there and overflow (they are placed after the zone later); most files fit.
        EXPECT_GT(all.size(), 4 * overflow.size());
        EXPECT_GT(placedClusters(all), 0U);
    }

}} // namespace icd::core::planner
