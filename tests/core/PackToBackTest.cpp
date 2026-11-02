// Pack-to-back (T087): video packed against the end of the volume without order (Fast, Moderate) and oldest at the
// end (Maximum); already packed videos never move; holes inside the zone are closed.

#include "core/planner/CandidateFilter.h"
#include "core/planner/PackToBack.h"
#include "core/planner/Validator.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <map>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::Effort;
    using strategy::ZoneAction;
    using strategy::ZoneOrder;

    // Video packed at the back: no order, by age at Maximum.
    strategy::ZoneScript videoScript() {
        strategy::Zone video = test::zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back);
        video.orderByLevel[static_cast<std::size_t>(Effort::Maximum)] = ZoneOrder::Age;
        return test::script({video});
    }

    // V newest, W older, X oldest.
    const test::PictureFacts videoFacts{.classes = {{'V', "video"}, {'W', "video"}, {'X', "video"}},
                                        .modified = {{'V', test::now - 10 * test::day},
                                                     {'W', test::now - 20 * test::day},
                                                     {'X', test::now - 30 * test::day}}};

    // A planned video zone and its validated plan.
    struct Packed {
        LowerBound bound;
        FillResult result;
        Plan plan;
        Validation validation;
    };

    // Plans the video zone of a picture at a level.
    Packed pack(const test::PictureVolume& v, Effort effort) {
        InlineExecutor serial;
        const auto s = videoScript();
        const ZoneAssignment a = test::assign(v, s);
        const TargetLayout layout = resolveLayout(v.snapshot, s, a, v.classes, classify::builtinCatalog(), {});
        Packed out;
        out.bound = lowerBound(v.snapshot, s, a, layout, {.wholeFiles = effort == Effort::Maximum}, serial);
        const auto sorted = applySortBounds(v.snapshot, s, a, layout, effort, 1.0, out.bound);
        const Candidates c = filterCandidates(out.bound, a, s.zones.size());
        HoleSet pool(v.snapshot.freeSpace.runs());
        for (std::size_t f = 0; f < v.snapshot.files.size(); ++f) {
            if (out.bound.mustMove[f] != 0) {
                releaseRuns(pool, movableRuns(v.snapshot, static_cast<FileIndex>(f)));
            }
        }
        const ZoneTarget& t = layout.zones.front();
        const SortedZone decision = sorted.empty() ? SortedZone{} : sorted.front();
        out.result = packToBack(v.snapshot, t, decision, c.perZone[0], pool,
                                {.zone = t.zone, .home = {t.firstLcn().value_or(0), t.length}});
        out.plan.lowerBound = out.bound.clusters;
        appendMoves(v.snapshot, out.result.placed, out.plan);
        for (const Move& m : out.plan.moves) {
            out.plan.extra += m.extra ? m.clusters : 0;
        }
        finishTotals(out.plan, v.snapshot);
        out.validation = validatePlan(v.snapshot, out.plan, {.layout = &layout});
        return out;
    }

    TEST(PackToBack, AlreadyPackedVideosNeverMove) {
        const auto v = test::pictureVolume("........VVWWXX", videoFacts);
        for (const Effort effort : strategy::allEfforts) {
            const Packed p = pack(v, effort);
            EXPECT_EQ(p.bound.clusters, 0U) << strategy::effortKey(effort);
            EXPECT_TRUE(p.plan.moves.empty());
        }
    }

    TEST(PackToBack, HolesInsideTheZoneAreClosed) {
        //  0 .. 5 6 7 8 9 10 11 12 13     zone [8,14): the hole [8,10) takes V
        //  . .. . V V . . W  W  X  X
        const auto v = test::pictureVolume("......VV..WWXX", videoFacts);
        const Packed p = pack(v, Effort::Moderate);
        EXPECT_EQ(p.bound.clusters, 2U);
        ASSERT_EQ(p.plan.moves.size(), 1U);
        EXPECT_EQ(p.plan.destinations(p.plan.moves[0])[0], (ClusterRun{8, 2}));
        EXPECT_TRUE(p.validation.ok()) << p.validation.issues.front().message;
    }

    TEST(PackToBack, MaximumPutsTheOldestAtTheEnd) {
        //  X X . . V V W W     zone [2,8); at Maximum the order is V, W, X (oldest last)
        const auto v = test::pictureVolume("XX..VVWW", videoFacts);
        const Packed moderate = pack(v, Effort::Moderate);
        EXPECT_EQ(moderate.plan.moves.size(), 1U); // No order: X just fills the hole [2,4).
        EXPECT_EQ(moderate.plan.destinations(moderate.plan.moves[0])[0], (ClusterRun{2, 2}));
        const Packed maximum = pack(v, Effort::Maximum);
        EXPECT_TRUE(maximum.validation.ok()) << maximum.validation.issues.front().message;
        std::map<char, Lcn> at;
        for (const Move& m : maximum.plan.moves) {
            for (const auto& [letter, file] : v.files) {
                if (file == m.file) {
                    at[letter] = maximum.plan.destinations(m)[0].start;
                }
            }
        }
        EXPECT_EQ(at, (std::map<char, Lcn>{{'V', 2}, {'W', 4}, {'X', 6}}));
        // The packed order puts V at 2 and W at 4: both sit two clusters behind their places in a zone without room,
        // so all three must move and nothing is extra.
        EXPECT_EQ(maximum.plan.lowerBound, 6U);
        EXPECT_EQ(maximum.plan.extra, 0U);
    }

}} // namespace icd::core::planner
