#include "services/workflow/Workflow.h"

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <vector>

namespace icd::services { namespace {

    using enum WorkflowState;
    using enum WorkflowAction;

    // Every action, for exhaustive checks.
    constexpr std::array allActions{SelectDrive, RefreshDrives, Analyze, Quick, Trim,   ChooseStrategy,
                                    BuildPlan,   Review,        Execute, Pause, Resume, Cancel};

    // A workflow with an HDD selected and analysed.
    Workflow analyzed() {
        Workflow flow;
        flow.selectDrive(DriveKind::Hdd);
        flow.apply(Analyze);
        flow.finish(JobOutcome::Succeeded);
        return flow;
    }

    // A workflow with a plan under review.
    Workflow reviewing() {
        Workflow flow = analyzed();
        flow.apply(BuildPlan);
        flow.finish(JobOutcome::Succeeded);
        return flow;
    }

    // Selecting a drive picks the start state from its kind.
    TEST(Workflow, SelectionSetsStartState) {
        Workflow flow;
        EXPECT_EQ(flow.state(), NoDrive);
        EXPECT_FALSE(flow.can(Analyze));
        EXPECT_TRUE(flow.selectDrive(DriveKind::Ssd));
        EXPECT_EQ(flow.state(), SsdReady);
        EXPECT_TRUE(flow.selectDrive(DriveKind::Unsupported));
        EXPECT_EQ(flow.state(), Unsupported);
        EXPECT_TRUE(flow.selectDrive(DriveKind::Hdd));
        EXPECT_EQ(flow.state(), NotAnalyzed);
        EXPECT_TRUE(flow.selectDrive(DriveKind::None));
        EXPECT_EQ(flow.state(), NoDrive);
    }

    // The whole workflow of plan.md section 11, one step at a time.
    TEST(Workflow, ClickThroughAllSteps) {
        Workflow flow;
        flow.selectDrive(DriveKind::Hdd);
        EXPECT_EQ(flow.apply(Analyze), WorkflowJob::Analyze);
        EXPECT_EQ(flow.state(), Analyzing);
        EXPECT_TRUE(flow.busy());
        EXPECT_EQ(flow.finish(JobOutcome::Succeeded), WorkflowJob::None);
        EXPECT_EQ(flow.state(), Analyzed);

        EXPECT_EQ(flow.apply(ChooseStrategy), WorkflowJob::None);
        EXPECT_EQ(flow.state(), Analyzed);
        EXPECT_EQ(flow.apply(BuildPlan), WorkflowJob::Plan);
        EXPECT_EQ(flow.state(), Planning);
        flow.finish(JobOutcome::Succeeded);
        EXPECT_EQ(flow.state(), Reviewing);

        EXPECT_EQ(flow.apply(Review), WorkflowJob::None);
        EXPECT_EQ(flow.state(), Reviewing);
        EXPECT_EQ(flow.apply(Execute), WorkflowJob::Execute);
        EXPECT_EQ(flow.state(), Executing);
        flow.apply(Pause);
        EXPECT_EQ(flow.state(), Paused);
        EXPECT_TRUE(flow.busy());
        flow.apply(Resume);
        EXPECT_EQ(flow.state(), Executing);
        flow.finish(JobOutcome::Succeeded);
        EXPECT_EQ(flow.state(), Executed);
        EXPECT_FALSE(flow.busy());
        EXPECT_TRUE(flow.can(Analyze)); // The layout changed: analyse again.
        EXPECT_FALSE(flow.can(Execute));
    }

    // Quick analyses, builds the plan by itself and stops at review.
    TEST(Workflow, QuickStopsAtReview) {
        Workflow flow;
        flow.selectDrive(DriveKind::Hdd);
        EXPECT_EQ(flow.apply(Quick), WorkflowJob::Analyze);
        EXPECT_TRUE(flow.quick());
        EXPECT_EQ(flow.finish(JobOutcome::Succeeded), WorkflowJob::Plan);
        EXPECT_EQ(flow.state(), Planning);
        EXPECT_TRUE(flow.quick());
        EXPECT_EQ(flow.finish(JobOutcome::Succeeded), WorkflowJob::None);
        EXPECT_EQ(flow.state(), Reviewing);
        EXPECT_FALSE(flow.quick());
    }

    // A plain analysis after a Quick run is not quick.
    TEST(Workflow, AnalyzeClearsQuick) {
        Workflow flow = reviewing();
        flow.apply(Quick);
        flow.selectDrive(DriveKind::Hdd); // Ignored: busy.
        EXPECT_TRUE(flow.quick());
        flow.finish(JobOutcome::Failed);
        EXPECT_FALSE(flow.quick());
        flow.apply(Analyze);
        EXPECT_FALSE(flow.quick());
        EXPECT_EQ(flow.finish(JobOutcome::Succeeded), WorkflowJob::None);
    }

    // Cancelling a Quick run during analysis goes back and never starts the plan.
    TEST(Workflow, CancelledQuickDoesNotPlan) {
        Workflow flow;
        flow.selectDrive(DriveKind::Hdd);
        flow.apply(Quick);
        EXPECT_TRUE(flow.can(Cancel));
        flow.apply(Cancel);
        EXPECT_TRUE(flow.cancelRequested());
        EXPECT_FALSE(flow.can(Cancel)); // Asked once; the job is stopping.
        EXPECT_FALSE(flow.quick());
        EXPECT_EQ(flow.finish(JobOutcome::Cancelled), WorkflowJob::None);
        EXPECT_EQ(flow.state(), NotAnalyzed);
        EXPECT_FALSE(flow.cancelRequested());
    }

    // A failed or cancelled step falls back to the last good state.
    TEST(Workflow, FailuresFallBack) {
        Workflow flow = analyzed();
        flow.apply(BuildPlan);
        flow.finish(JobOutcome::Failed);
        EXPECT_EQ(flow.state(), Analyzed);

        flow = reviewing();
        flow.apply(Execute);
        flow.apply(Pause);
        flow.apply(Cancel);
        flow.finish(JobOutcome::Cancelled);
        EXPECT_EQ(flow.state(), Executed); // Some moves may have happened.

        flow.selectDrive(DriveKind::Hdd);
        flow.apply(Analyze);
        flow.finish(JobOutcome::Failed);
        EXPECT_EQ(flow.state(), NotAnalyzed);
    }

    // Choosing another strategy drops the plan under review.
    TEST(Workflow, ChoosingStrategyDropsPlan) {
        Workflow flow = reviewing();
        EXPECT_TRUE(flow.can(Execute));
        flow.apply(ChooseStrategy);
        EXPECT_EQ(flow.state(), Analyzed);
        EXPECT_FALSE(flow.can(Execute));
        EXPECT_TRUE(flow.can(BuildPlan));
    }

    // SSDs only TRIM; HDDs never TRIM; unsupported drives do nothing.
    TEST(Workflow, DriveKindsLimitActions) {
        Workflow flow;
        flow.selectDrive(DriveKind::Ssd);
        EXPECT_FALSE(flow.can(Analyze));
        EXPECT_FALSE(flow.can(Quick));
        EXPECT_EQ(flow.apply(Trim), WorkflowJob::Trim);
        EXPECT_EQ(flow.state(), Trimming);
        EXPECT_TRUE(flow.can(Cancel));
        flow.finish(JobOutcome::Succeeded);
        EXPECT_EQ(flow.state(), SsdReady);

        flow.selectDrive(DriveKind::Hdd);
        EXPECT_FALSE(flow.can(Trim));
        flow.selectDrive(DriveKind::Unsupported);
        for (const WorkflowAction action : allActions) {
            const bool expected = action == SelectDrive || action == RefreshDrives;
            EXPECT_EQ(flow.can(action), expected) << static_cast<int>(action);
        }
    }

    // While a job runs the drive cannot change.
    TEST(Workflow, BusyLocksSelection) {
        Workflow flow;
        flow.selectDrive(DriveKind::Hdd);
        flow.apply(Analyze);
        EXPECT_FALSE(flow.can(SelectDrive));
        EXPECT_FALSE(flow.can(RefreshDrives));
        EXPECT_FALSE(flow.selectDrive(DriveKind::Ssd));
        EXPECT_EQ(flow.state(), Analyzing);
    }

    // In every reachable state, a disallowed action returns no job and changes nothing.
    TEST(Workflow, DisallowedActionsChangeNothing) {
        const std::vector<std::function<Workflow()>> setups{
            [] { return Workflow(); },
            [] {
                Workflow f;
                f.selectDrive(DriveKind::Ssd);
                return f;
            },
            [] {
                Workflow f;
                f.selectDrive(DriveKind::Ssd);
                f.apply(Trim);
                return f;
            },
            [] {
                Workflow f;
                f.selectDrive(DriveKind::Unsupported);
                return f;
            },
            [] {
                Workflow f;
                f.selectDrive(DriveKind::Hdd);
                return f;
            },
            [] {
                Workflow f;
                f.selectDrive(DriveKind::Hdd);
                f.apply(Analyze);
                return f;
            },
            [] { return analyzed(); },
            [] {
                Workflow f = analyzed();
                f.apply(BuildPlan);
                return f;
            },
            [] { return reviewing(); },
            [] {
                Workflow f = reviewing();
                f.apply(Execute);
                return f;
            },
            [] {
                Workflow f = reviewing();
                f.apply(Execute);
                f.apply(Pause);
                return f;
            },
            [] {
                Workflow f = reviewing();
                f.apply(Execute);
                f.finish(JobOutcome::Succeeded);
                return f;
            },
        };
        for (const auto& setup : setups) {
            for (const WorkflowAction action : allActions) {
                Workflow flow = setup();
                if (flow.can(action)) {
                    continue;
                }
                const WorkflowState before = flow.state();
                EXPECT_EQ(flow.apply(action), WorkflowJob::None);
                EXPECT_EQ(flow.state(), before) << static_cast<int>(action);
            }
        }
    }

    // Finishing with no job running changes nothing.
    TEST(Workflow, FinishWithoutJobIsIgnored) {
        Workflow flow = analyzed();
        EXPECT_EQ(flow.finish(JobOutcome::Succeeded), WorkflowJob::None);
        EXPECT_EQ(flow.state(), Analyzed);
    }

    // Drive kinds come from media and file system.
    TEST(Workflow, DriveKindFromDrive) {
        core::DriveInfo drive;
        drive.fileSystem = "NTFS";
        drive.media = core::MediaType::Hdd;
        EXPECT_EQ(driveKind(&drive), DriveKind::Hdd);
        drive.media = core::MediaType::Ssd;
        EXPECT_EQ(driveKind(&drive), DriveKind::Ssd);
        drive.media = core::MediaType::Hdd;
        drive.fileSystem = "exFAT";
        EXPECT_EQ(driveKind(&drive), DriveKind::Unsupported);
        EXPECT_EQ(driveKind(nullptr), DriveKind::None);
    }

}} // namespace icd::services
