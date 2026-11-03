// The live map (T111): finished moves applied to the shown map in batches give the same map as building it anew from
// the snapshot after all of them, so recomputing only the moved files' clusters misses nothing; a patch that does not
// fit the map is refused.

#include "services/execute/LiveMap.h"

#include "PlannedBed.h"
#include "SimDevice.h"
#include "services/execute/ExecutedSnapshot.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace icd::services { namespace {

    using testing::SimDevice;

    // Every stored level of two pyramids is equal.
    void expectSamePyramid(const core::Pyramid& a, const core::Pyramid& b) {
        ASSERT_EQ(a.baseLevel(), b.baseLevel());
        ASSERT_EQ(a.topLevel(), b.topLevel());
        for (int level = a.baseLevel(); level <= a.topLevel(); ++level) {
            EXPECT_TRUE(std::ranges::equal(a.level(level), b.level(level))) << "level " << level;
        }
    }

    TEST(LiveMap, BatchesOfMovesGiveTheMapOfTheVolumeAfter) {
        const testing::PlannedBed b = testing::plannedBed(31, core::strategy::Effort::Maximum);
        ASSERT_GT(b.plan->moves.size(), 20U);
        SimDevice device(*b.volume);
        const auto report = executePlan(*b.plan, core::execute::allMoves(*b.plan), device, {}, {});
        ASSERT_EQ(report.totals.skipped, 0U);
        core::InlineExecutor serial;
        const core::ScreenAspect aspect{16, 9};
        const auto shown = core::MapData::build(*b.snapshot, aspect, serial, {}, b.classes);

        const std::span<const MoveRecord> records = report.records;
        const std::size_t half = records.size() / 2;
        const auto first = applyMoves(*shown, *b.plan, records.first(half), serial);
        ASSERT_TRUE(first);
        EXPECT_FALSE(first->dirty.empty());
        EXPECT_TRUE(std::ranges::is_sorted(first->dirty, {}, &core::ClusterRun::start));
        const auto second = applyMoves(*first->map, *b.plan, records.subspan(half), serial);
        ASSERT_TRUE(second);

        core::Snapshot base = *b.snapshot;
        auto after = snapshotAfter(base, *b.plan, records, serial);
        ASSERT_TRUE(after);
        const auto rebuilt = core::MapData::build(std::move(*after), aspect, serial, {}, b.classes);
        expectSamePyramid(second->map->pyramid(), rebuilt->pyramid());
        EXPECT_EQ(second->map->ageReference(), rebuilt->ageReference());
        EXPECT_EQ(second->map->world().rowWidth(), shown->world().rowWidth()); // The world never re-flows.
        EXPECT_TRUE(std::ranges::equal(second->map->fileClasses(), b.classes));
    }

    // The volume changed between the analysis and the run (here: the first half of the moves, as if made by someone
    // else): the live map rebases on the volume revalidation read, and the rest of the moves then fit.
    TEST(LiveMap, RebasesOnTheVolumeNowThenAppliesTheRest) {
        const testing::PlannedBed b = testing::plannedBed(33, core::strategy::Effort::Maximum);
        SimDevice device(*b.volume);
        const auto report = executePlan(*b.plan, core::execute::allMoves(*b.plan), device, {}, {});
        core::InlineExecutor serial;
        const core::ScreenAspect aspect{16, 9};
        const auto shown = core::MapData::build(*b.snapshot, aspect, serial, {}, b.classes);
        EXPECT_EQ(rebaseMap(*shown, *b.snapshot, serial), nullptr); // Same layout: nothing to rebuild.

        const std::span<const MoveRecord> records = report.records;
        const std::size_t half = records.size() / 2;
        core::Snapshot copy = *b.snapshot;
        auto now = snapshotAfter(copy, *b.plan, records.first(half), serial);
        ASSERT_TRUE(now);
        const auto rebased = rebaseMap(*shown, *now, serial);
        ASSERT_NE(rebased, nullptr);
        expectSamePyramid(rebased->pyramid(), core::MapData::build(*now, aspect, serial, {}, b.classes)->pyramid());
        EXPECT_TRUE(std::ranges::equal(rebased->fileClasses(), b.classes)); // Carried over by file reference.

        const auto rest = applyMoves(*rebased, *b.plan, records.subspan(half), serial);
        ASSERT_TRUE(rest);
        core::Snapshot all = *b.snapshot;
        auto after = snapshotAfter(all, *b.plan, records, serial);
        ASSERT_TRUE(after);
        expectSamePyramid(rest->map->pyramid(),
                          core::MapData::build(std::move(*after), aspect, serial, {}, b.classes)->pyramid());
    }

    TEST(LiveMap, NothingToApplyOrAPatchThatDoesNotFit) {
        const testing::PlannedBed b = testing::plannedBed(32, core::strategy::Effort::Moderate);
        ASSERT_FALSE(b.plan->moves.empty());
        core::InlineExecutor serial;
        const auto shown = core::MapData::build(*b.snapshot, {16, 9}, serial, {}, b.classes);
        EXPECT_FALSE(applyMoves(*shown, *b.plan, {}, serial));
        MoveRecord skipped;
        skipped.outcome = MoveOutcome::Skipped;
        EXPECT_FALSE(applyMoves(*shown, *b.plan, std::span(&skipped, 1), serial)); // Nothing moved.

        // A move that claims to have landed on clusters another file holds: the volume is not what the map shows.
        const auto& extents = b.snapshot->extents.records();
        const auto planned = b.snapshot->extents.extentsOf(
            *core::execute::SnapshotFiles(*b.snapshot, *b.plan).indexOf(b.plan->moves.front().fileReference));
        const auto other = std::ranges::find_if(extents, [&](const core::ExtentRecord& r) {
            return std::ranges::none_of(planned, [&](std::uint32_t p) { return extents[p].file() == r.file(); });
        });
        ASSERT_NE(other, extents.end());
        MoveRecord wrong;
        wrong.move = 0;
        wrong.outcome = MoveOutcome::Moved;
        wrong.clustersMoved = 1;
        wrong.runsAfter = {{.stream = 0, .vcn = 0, .lcn = other->run().start, .length = 1}};
        EXPECT_FALSE(applyMoves(*shown, *b.plan, std::span(&wrong, 1), serial));
        EXPECT_TRUE(anyApplicable(std::span(&wrong, 1)));
        EXPECT_FALSE(anyApplicable(std::span(&skipped, 1)));
    }

}} // namespace icd::services
