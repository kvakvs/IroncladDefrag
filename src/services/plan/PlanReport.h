#pragma once

#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"
#include "services/plan/PlanJob.h"

#include <optional>
#include <string>

namespace icd::services {

    // What a plan report holds.
    struct PlanReportOptions {
        std::optional<core::strategy::Effort> level{}; // Only this level; all three when empty.
        bool moves = false; // Every move of the reported levels: file, reason, zone, sources, destinations.
        bool timing = true; // Wall-clock planning time (left out to compare reports of the same snapshot).
    };

    // The plan of a request as JSON (icd-cli plan --json): the strategy, then per level its totals, lower bound and the
    // ratio to it, extra, skipped and deferred bytes, metrics before and after, time estimate, hot spots, notes and
    // validation issues, and optionally its moves. Built from the same outcome the Plan tab shows, so the two agree.
    [[nodiscard]] std::string planJson(const PlanOutcome& outcome, const PlanRequest& request,
                                       const PlanReportOptions& options);

    // The same as readable text.
    [[nodiscard]] std::string planText(const PlanOutcome& outcome, const PlanRequest& request,
                                       const PlanReportOptions& options);

} // namespace icd::services
