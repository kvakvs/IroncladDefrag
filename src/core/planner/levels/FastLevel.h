#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/planner/LevelPlanner.h"
#include "core/snapshot/Snapshot.h"

#include <span>
#include <vector>

namespace icd::core::planner {

    // How hot spots are found (plan.md 9.2.1 step 1).
    struct HotSpotOptions {
        double threshold = 0.25; // Stripes scoring at least this are hot.
        std::uint64_t smallHoleBytes = 1ULL << 20U; // Free runs under this count as small holes.
        std::uint32_t holesForFullScore = 32; // Small holes in a stripe that give the full hole score.
    };

    // One stripe's churn score: weighted shares of churning files, small holes and fragmented files.
    struct StripeScore {
        double churn = 0.0; // Share of the stripe held by churning (or in-place) files.
        double holes = 0.0; // Small holes, as a share of holesForFullScore (at most 1).
        double fragments = 0.0; // Share of the stripe's used clusters held by fragmented files.
        double score = 0.0; // 0.4 churn + 0.3 holes + 0.3 fragments.
    };

    // Scores every stripe of the volume (in parallel, the same for any executor).
    [[nodiscard]] std::vector<StripeScore> scoreStripes(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                                        std::span<const std::uint8_t> churn,
                                                        const HotSpotOptions& options, Executor& executor);

    // Hot spots: runs of stripes at or above the threshold, merged with their neighbours (one cold stripe between two
    // hot ones is bridged), in LCN order.
    [[nodiscard]] std::vector<ClusterRun> findHotSpots(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                                       std::span<const std::uint8_t> churn,
                                                       const HotSpotOptions& options, Executor& executor);

    // The Fast level's policy: zone fragment_ok, no sorting, no parking, budget 2% of used space or 15 minutes, both
    // scaled by the settings multiplier (plan.md 9.2, 9.2.1 step 7).
    [[nodiscard]] LevelPolicy fastPolicy(const PlanInput& input);

    // Plans the Fast level (plan.md 9.2.1): only files lying wholly inside hot spots move. Fragmented files are joined
    // and each hot spot is compacted towards its head, so its free space becomes one run at its tail where new churn
    // lands; a big long-lived file (64 MB or more, unchanged for 30 days) that splits a hot spot's free space goes
    // straight to its zone instead. Cut to the budget, most valuable moves first.
    [[nodiscard]] PlanResult planFast(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                                      const CancelToken& token = {}, const HotSpotOptions& options = {});

} // namespace icd::core::planner
