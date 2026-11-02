#pragma once

#include "core/strategy/ZoneScript.h"
#include "services/execute/Execution.h"
#include "services/job/JobRunner.h"
#include "services/plan/PlanJob.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icd::services {

    // The moved files read again by a full rescan after execution (plan.md 12: the plan's predicted layout must match
    // a rescan), and the saved snapshot against the same rescan.
    struct RescanCheck {
        std::uint64_t filesChecked = 0; // Moved files compared.
        std::uint64_t mismatches = 0; // Files the rescan finds elsewhere than predicted.
        std::vector<std::string> samples; // The first mismatches.
        std::optional<std::uint64_t> snapshotDifferences; // Saved snapshot vs rescan (access times left out), if saved.
        std::vector<std::string> snapshotSamples; // The first of those differences.
    };

    // Scans the volume in full (nothing saved) and compares every moved file's runs with its prediction: a file whose
    // last move completed at its planned destination must lie exactly there, any other moved file where the executor
    // read it back. Then compares the saved snapshot, if execution saved one, with the rescan.
    [[nodiscard]] RescanCheck checkAgainstRescan(const ExecutionRequest& request, const ExecutionOutcome& outcome,
                                                 JobContext& context);

    // A placed zone of the plan, for reports (where the churn gap is, plan.md 8.3).
    struct ZoneSpan {
        std::string name; // Zone name from the script.
        std::vector<core::ClusterRun> pieces; // Its target runs.
        std::vector<core::ClusterRun> gap; // The gap after it.
    };

    // The placed zones of one level of a plan.
    [[nodiscard]] std::vector<ZoneSpan> zoneSpans(const PlanOutcome& outcome, const PlanRequest& request,
                                                  core::strategy::Effort level);

    // What an execution report shows.
    struct ExecuteReportInput {
        const ExecutionRequest* request = nullptr; // The request (plan, drive, log).
        const ExecutionOutcome* outcome = nullptr; // What happened.
        const RescanCheck* rescan = nullptr; // The rescan check, if one ran.
        std::vector<ZoneSpan> zones; // The plan's zones, when it was planned in the same run.
        bool records = false; // Every move's record (icd-cli execute --moves).
    };

    // The report as JSON (icd-cli execute --json): plan, revalidation with every drop, execution totals, every move
    // that did not complete with its reason, the snapshot, the rescan check, zones and optionally every record.
    [[nodiscard]] std::string executeJson(const ExecuteReportInput& input);

    // The report as text.
    [[nodiscard]] std::string executeText(const ExecuteReportInput& input);

} // namespace icd::services
