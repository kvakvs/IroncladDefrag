#pragma once

// A planned synthetic volume and the volume in memory to run its plan on, shared by the tests of the GUI's execution
// (feed, live map).

#include "SimVolume.h"
#include "core/classify/RuleEngine.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/plan/PlanJob.h"

#include <memory>
#include <vector>

namespace icd::testing {

    // A planned volume, its classes and plan, and where it runs.
    struct PlannedBed {
        std::shared_ptr<core::Snapshot> snapshot; // Planned volume.
        std::vector<std::uint8_t> classes; // Per file, as the map is built with.
        std::shared_ptr<core::execute::ExecutionPlan> plan; // The plan of the level.
        std::unique_ptr<SimVolume> volume; // Where it runs.
    };

    // Plans a seeded 1 GB volume with the default preset at a level.
    inline PlannedBed plannedBed(std::uint64_t seed, core::strategy::Effort level) {
        core::SynthOptions options;
        options.seed = seed;
        options.files = 800;
        options.geometry.totalClusters = 1ULL << 18U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.3;
        PlannedBed b;
        b.snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        services::PlanRequest r;
        r.script = core::strategy::builtinPresets().front();
        r.now = 20'000LL * 864'000'000'000LL;
        core::InlineExecutor serial;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        b.classes = engine.classify(*b.snapshot, {.now = r.now, .churn = {}}, serial);
        r.classes = b.classes;
        r.snapshot = b.snapshot;
        const services::PlanOutcome outcome = services::planLevels(r, serial);
        core::execute::PlanIdentity id;
        id.geometry = b.snapshot->geometry;
        b.plan = std::make_shared<core::execute::ExecutionPlan>(
            core::execute::executionPlanOf(outcome.levels.at(static_cast<std::size_t>(level)).result, *b.snapshot, id));
        b.volume = std::make_unique<SimVolume>(*b.snapshot);
        return b;
    }

} // namespace icd::testing
