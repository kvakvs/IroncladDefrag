// Planning orchestration (T093): the three levels planned in parallel on a thread pool equal the serial plans exactly,
// every level validates, and the Plan tab summary carries the figures.

#include "services/plan/PlanJob.h"

#include "core/classify/RuleEngine.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    using core::strategy::Anchor;
    using core::strategy::ZoneAction;

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // A zone of catalog classes.
    core::strategy::Zone zone(std::string name, std::vector<std::string> classes, ZoneAction action,
                              Anchor anchor = Anchor::AfterPrevious) {
        core::strategy::Zone z;
        z.name = std::move(name);
        z.select.classes = std::move(classes);
        z.action = action;
        z.anchor = anchor;
        return z;
    }

    // A request on a synthetic volume under a mixed strategy.
    PlanRequest request(std::uint64_t seed) {
        core::SynthOptions options;
        options.seed = seed;
        options.files = 4000;
        options.geometry.totalClusters = 1ULL << 21U;
        options.fillPercent = 60;
        options.fragmentedShare = 0.3;
        auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        core::strategy::Zone photos = zone("Photos", {"photos"}, ZoneAction::Sort, Anchor::Front);
        photos.order = core::strategy::ZoneOrder::DirectoryName;
        core::strategy::Zone video = zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back);
        video.orderByLevel[2] = core::strategy::ZoneOrder::Age;
        r.script = {
            .id = "t",
            .name = "Test",
            .description = "",
            .zones = {photos,
                      zone("Rest", {"unknown", "archives", "source", "disc-images", "backups"}, ZoneAction::FastFill),
                      video}};
        r.now = 20'000 * day;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        core::InlineExecutor serial;
        r.classes = engine.classify(*snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = std::move(snapshot);
        return r;
    }

    // Whether two plans are the same move for move.
    void expectSamePlans(const core::planner::Plan& a, const core::planner::Plan& b) {
        ASSERT_EQ(a.moves.size(), b.moves.size());
        EXPECT_EQ(a.runs, b.runs);
        for (std::size_t i = 0; i < a.moves.size(); ++i) {
            const auto& x = a.moves[i];
            const auto& y = b.moves[i];
            ASSERT_EQ(std::tie(x.file, x.reason, x.zone, x.fromParking, x.extra, x.clusters),
                      std::tie(y.file, y.reason, y.zone, y.fromParking, y.extra, y.clusters))
                << i;
        }
        EXPECT_EQ(a.lowerBound, b.lowerBound);
        EXPECT_EQ(a.skippedClusters, b.skippedClusters);
        EXPECT_EQ(a.deferredClusters, b.deferredClusters);
        EXPECT_EQ(a.extra, b.extra);
    }

    TEST(PlanJob, ParallelPlansEqualSerialPlans) {
        for (const std::uint64_t seed : {71ULL, 72ULL}) {
            const PlanRequest r = request(seed);
            core::InlineExecutor serial;
            const PlanOutcome one = planLevels(r, serial);
            ThreadPool pool(6);
            const PlanOutcome many = planLevels(r, pool);
            PlanRequest sequential = r;
            sequential.parallelLevels = false;
            const PlanOutcome ordered = planLevels(sequential, pool);
            for (std::size_t level = 0; level < 3; ++level) {
                SCOPED_TRACE(level);
                EXPECT_TRUE(one.levels[level].issues.empty()) << one.levels[level].issues.front();
                EXPECT_TRUE(many.levels[level].issues.empty());
                expectSamePlans(one.levels[level].result.plan, many.levels[level].result.plan);
                expectSamePlans(one.levels[level].result.plan, ordered.levels[level].result.plan);
                EXPECT_FALSE(one.levels[level].result.plan.moves.empty());
            }
        }
    }

    TEST(PlanJob, SummaryCarriesTheFigures) {
        const PlanRequest r = request(73);
        core::InlineExecutor serial;
        const PlanOutcome outcome = planLevels(r, serial);
        const PlanSummary summary = planSummary(outcome);
        for (std::size_t level = 0; level < 3; ++level) {
            const auto& plan = outcome.levels[level].result.plan;
            EXPECT_EQ(summary.levels[level].bytesToMove, plan.totals.clustersMoved * 4096);
            EXPECT_EQ(summary.levels[level].filesToMove, plan.totals.files);
            EXPECT_EQ(summary.levels[level].lowerBoundBytes, plan.lowerBound * 4096);
        }
        EXPECT_EQ(summary.levels[0].level, EffortLevel::Fast);
        EXPECT_LE(summary.levels[1].bytesToMove, summary.levels[2].bytesToMove);
        EXPECT_FALSE(summary.warnings.empty()); // At least Fast's "run it again".
    }

    TEST(PlanJob, JobStoresTheOutcomeAndReportsProgress) {
        auto output = std::make_shared<PlanOutput>();
        JobFunction job = makePlanJob(request(74), output);
        struct Sink : core::ProgressSink {
            void report(const core::Progress& p) override { last = p; }
            core::Progress last;
        } sink;
        core::InlineExecutor serial;
        JobContext context{.token = {}, .progress = sink, .executor = serial};
        job(context);
        const auto outcome = output->take();
        ASSERT_TRUE(outcome);
        EXPECT_EQ(sink.last.stage, "Planning");
        EXPECT_EQ(sink.last.done, 3U);
        EXPECT_FALSE(output->take());
    }

}} // namespace icd::services
