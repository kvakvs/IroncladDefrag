#pragma once

#include "core/classify/Catalog.h"
#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/planner/TargetLayout.h"
#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace icd::core::planner {

    // How strict "good enough" is at an effort level (plan.md 9.2, 9.3.3).
    struct GoodEnough {
        bool wholeFiles = false; // Maximum: every movable file must be one fragment (fragment_ok = whole file).
    };

    // Whether a file's runs are good enough: one fragment, or every fragment at least fragmentOk bytes (nullopt: the
    // whole file must be one fragment).
    [[nodiscard]] bool fragmentsOk(std::span<const ClusterRun> runs, std::optional<std::uint64_t> fragmentOk,
                                   std::uint32_t bytesPerCluster);

    // The fragment_ok that applies to a file's zone (the default for files without one) at a level.
    [[nodiscard]] std::optional<std::uint64_t> fragmentOkFor(const strategy::ZoneScript& script, ZoneIndex zone,
                                                             const GoodEnough& rules);

    // Which files must move to reach a target, and how much that is (plan.md 9.3.1): a Placed file must move unless it
    // lies inside its zone with good-enough fragments; a defragment-only file unless its fragments are good enough;
    // fixed and in-place files never.
    struct LowerBound {
        std::vector<std::uint8_t> mustMove{}; // Per file: 1 when it must move.
        ClusterCount clusters = 0; // Movable clusters of those files.
        std::vector<ClusterCount> perZone{}; // Per script zone, plus one last slot for files without a zone.
    };

    // The span of a file's movable runs in usable cluster numbers [lo, hi), or nullopt when any run touches an
    // obstacle (stay tests compare it with zone positions on the axis).
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>>
    usableSpan(const Snapshot& snapshot, FileIndex file, const AvailableAxis& axis);

    // The lower bound of a layout; the same for any executor.
    [[nodiscard]] LowerBound lowerBound(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                        const ZoneAssignment& assignment, const TargetLayout& layout,
                                        const GoodEnough& rules, Executor& executor, const CancelToken& token = {});

    // Chooses where the slack goes (LayoutInputs::slackBefore) so the lower bound is lowest: zone boundaries move
    // towards where files already are instead of files moving towards a fixed boundary (plan.md 9.3.2). Exact over all
    // slack splits while the candidate positions are few (small volumes); on large volumes the candidates are thinned
    // to `maxCandidates` evenly spread ones. Returns `base` with slackBefore filled in.
    [[nodiscard]] LayoutInputs fitTarget(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                         const ZoneAssignment& assignment, std::span<const classify::ClassId> classes,
                                         const classify::Catalog& catalog, LayoutInputs base, const GoodEnough& rules,
                                         Executor& executor, std::size_t maxCandidates = 1024);

} // namespace icd::core::planner
