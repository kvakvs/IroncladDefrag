// Defragment this file (T128): one file into one free run near its start, else into large pieces when that leaves
// fewer fragments, never into the MFT zone; refusals for metadata, unfragmented files and no room; every plan passes
// the validator and becomes an execution plan with the file's path.

#include "services/execute/FileDefrag.h"

#include "core/PlannerTestUtil.h"
#include "core/execute/ExecutionPlan.h"
#include "core/planner/Validator.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    using core::planner::test::pictureVolume;

    // The destination runs of the plan's only move.
    std::vector<core::ClusterRun> destinations(const FileDefragPlan& planned) {
        const core::planner::Plan& plan = planned.result.plan;
        EXPECT_EQ(plan.moves.size(), 1U);
        const auto runs = plan.destinations(plan.moves.front());
        return {runs.begin(), runs.end()};
    }

    TEST(FileDefrag, WholeFileIntoTheNearestRun) {
        //                                    0         1         2         3
        //                                    0123456789012345678901234567890123
        const auto v = pictureVolume("aa..bbb.aa.b.....cc.......aaa.......");
        const core::FileIndex a = v.files.at('a');
        const FileDefragPlan planned = planFileDefrag(v.snapshot, a, {});
        ASSERT_FALSE(planned.refusal) << *planned.refusal;
        const auto runs = destinations(planned);
        ASSERT_EQ(runs.size(), 1U);
        EXPECT_EQ(runs.front().length, 7U);
        EXPECT_EQ(runs.front().start, 19U); // The nearest free run that holds 7 (the one at 12 holds 5).
        EXPECT_TRUE(core::planner::validatePlan(v.snapshot, planned.result.plan).ok());
        const core::execute::ExecutionPlan execution =
            core::execute::executionPlanOf(planned.result, v.snapshot, {}, {});
        ASSERT_EQ(execution.moves.size(), 1U);
        EXPECT_EQ(execution.paths.front(), "\\a");
    }

    TEST(FileDefrag, NeverIntoTheReservedZone) {
        const auto v = pictureVolume("aa..bbb.aa.b.....cc.......aaa.......");
        const std::array reserved{core::ClusterRun{19, 7}}; // The nearest fitting run is the "MFT zone".
        const FileDefragPlan planned = planFileDefrag(v.snapshot, v.files.at('a'), reserved);
        ASSERT_FALSE(planned.refusal) << *planned.refusal;
        const auto runs = destinations(planned);
        ASSERT_EQ(runs.size(), 1U);
        EXPECT_EQ(runs.front().start, 29U);
    }

    TEST(FileDefrag, LargePiecesWhenNoRunHoldsTheFile) {
        // 64 MB pieces are 16 clusters of 4 MB: the two free runs of 16 take the 32-cluster file in two pieces, fewer
        // than its three.
        core::planner::test::PictureFacts facts;
        facts.clusterBytes = 4U << 20U;
        const std::string picture = std::string(12, 'a') + "b" + std::string(12, 'a') + "b" + std::string(8, 'a') +
            "b" + std::string(16, '.') + "b" + std::string(16, '.') + "b";
        const auto v = pictureVolume(picture, facts);
        const FileDefragPlan planned = planFileDefrag(v.snapshot, v.files.at('a'), {});
        ASSERT_FALSE(planned.refusal) << *planned.refusal;
        const auto runs = destinations(planned);
        EXPECT_EQ(runs.size(), 2U);
        EXPECT_TRUE(core::planner::validatePlan(v.snapshot, planned.result.plan).ok());
    }

    TEST(FileDefrag, Refusals) {
        const auto v = pictureVolume("##..aa.bbbb.b.c.c");
        EXPECT_TRUE(planFileDefrag(v.snapshot, v.files.at('#'), {}).refusal); // Metadata.
        EXPECT_TRUE(planFileDefrag(v.snapshot, v.files.at('a'), {}).refusal); // One piece.
        EXPECT_EQ(fileDefragRefusal(v.snapshot, v.files.at('b')), std::nullopt);
        const FileDefragPlan full = planFileDefrag(v.snapshot, v.files.at('b'), {}); // 5 clusters, free runs of 1-2.
        ASSERT_TRUE(full.refusal);
        EXPECT_NE(full.refusal->find("no free space"), std::string::npos);
        EXPECT_TRUE(full.result.plan.moves.empty());
        EXPECT_TRUE(fileDefragRefusal(v.snapshot, 1000)); // Not on the map.
    }

}} // namespace icd::services
