// plan-bench: the planning target of plan.md 6.5 (30 TB / 20 M files planned in 60 s or less on 8 cores) on a
// synthetic volume, without a real drive. Builds the volume, classifies it with the built-in catalog, plans the three
// levels of a mixed strategy in parallel on N workers (validating each plan), and prints per-level figures: moves,
// bytes, lower bound and the ratio, skipped and deferred bytes, zone fit and fragmentation after, wall time, a
// fingerprint of the plan (so a change that should not alter plans can be checked), and the stages that took half a
// second or more.
// --compare 1 plans again on one worker and checks the plans are identical. On a CPU with efficiency cores, measure on
// eight performance cores (PowerShell: start it, then `$p.ProcessorAffinity = 0x5555`): serial stages that land on an
// efficiency core run up to twice as long.
//
// Usage: plan-bench [--files N] [--tb N] [--threads N] [--compare 0|1] [--validate 0|1]

#include "core/classify/Catalog.h"
#include "core/classify/RuleEngine.h"
#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"
#include "services/plan/PlanJob.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

    using namespace icd;
    using Clock = std::chrono::steady_clock;

    // Command-line settings.
    struct Args {
        std::uint32_t files = 20'000'000; // Regular files.
        std::uint64_t terabytes = 30; // Volume size.
        std::size_t threads = 8; // Workers.
        bool compare = false; // Plan on one worker too and compare.
        bool validate = true; // Validate every plan.
    };

    // Seconds since start.
    double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

    // Parses "--name value" pairs; unknown names are ignored.
    Args parse(int argc, char** argv) {
        Args a;
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::string name = argv[i];
            const std::uint64_t value = std::strtoull(argv[i + 1], nullptr, 10);
            a.files = name == "--files" ? static_cast<std::uint32_t>(value) : a.files;
            a.terabytes = name == "--tb" ? value : a.terabytes;
            a.threads = name == "--threads" ? value : a.threads;
            a.compare = name == "--compare" ? value != 0 : a.compare;
            a.validate = name == "--validate" ? value != 0 : a.validate;
        }
        return a;
    }

    // A zone of catalog classes.
    core::strategy::Zone zone(std::string name, std::vector<std::string> classes, core::strategy::ZoneAction action,
                              core::strategy::Anchor anchor = core::strategy::Anchor::AfterPrevious) {
        core::strategy::Zone z;
        z.name = std::move(name);
        z.select.classes = std::move(classes);
        z.action = action;
        z.anchor = anchor;
        return z;
    }

    // The mixed strategy of plan.md 8.7 in short.
    core::strategy::ZoneScript mixed() {
        using core::strategy::ZoneAction;
        auto dirs = zone("Directories", {"directories"}, ZoneAction::Sort, core::strategy::Anchor::Front);
        dirs.order = core::strategy::ZoneOrder::DirectoryName;
        auto photos = zone("Photos", {"photos"}, ZoneAction::Sort);
        photos.order = core::strategy::ZoneOrder::DirectoryName;
        photos.gapAfter.freePercent = 2;
        auto video = zone("Video", {"video"}, ZoneAction::PackToBack, core::strategy::Anchor::Back);
        video.orderByLevel[2] = core::strategy::ZoneOrder::Age;
        return {.id = "mixed",
                .name = "Mixed",
                .description = "",
                .zones = {dirs, zone("Churn", {"temp", "build"}, ZoneAction::KeepInPlace), photos,
                          zone("Rest",
                               {"unknown", "archives", "disc-images", "vm-disks", "backups", "source", "dependencies",
                                "repository", "catalogs"},
                               ZoneAction::FastFill),
                          video}};
    }

    // A hash of a plan's moves, runs and skipped files, so runs of the bench show whether a change altered the plans.
    std::uint64_t fingerprint(const core::planner::Plan& p) {
        std::uint64_t h = 1469598103934665603ULL; // FNV-1a.
        const auto mix = [&](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        for (const auto& m : p.moves) {
            mix(m.file);
            mix(static_cast<std::uint64_t>(m.reason) | (std::uint64_t{m.zone} << 8U) |
                (std::uint64_t{m.fromParking} << 40U) | (std::uint64_t{m.extra} << 41U));
        }
        for (const auto& r : p.runs) {
            mix(r.start);
            mix(r.length);
        }
        for (const auto& s : p.skipped) {
            mix(s.file);
        }
        return h;
    }

    // Prints one level.
    void printLevel(const char* name, const services::LevelOutcome& o, std::uint32_t cluster) {
        const auto& p = o.result.plan;
        const double ratio = p.lowerBound == 0 ? 1.0
                                               : static_cast<double>(p.totals.clustersMoved) /
                static_cast<double>(p.lowerBound - p.skippedClusters - p.deferredClusters + 1);
        std::printf("  %-9s %8.2f s  %10s moves  %9s moved  bound %9s  ratio %.3f  skipped %9s  deferred %9s  "
                    "fit %.1f%% -> %.1f%%  fragmented %.1f%% -> %.1f%%  plan %016llx  %s\n",
                    name, o.seconds, core::formatCount(p.moves.size()).c_str(),
                    core::formatBytes(p.totals.clustersMoved * cluster).c_str(),
                    core::formatBytes(p.lowerBound * cluster).c_str(), ratio,
                    core::formatBytes(p.skippedClusters * cluster).c_str(),
                    core::formatBytes(p.deferredClusters * cluster).c_str(), o.result.metrics.zoneFitBefore,
                    o.result.metrics.zoneFitAfter, o.result.metrics.fragmentedBefore, o.result.metrics.fragmentedAfter,
                    static_cast<unsigned long long>(fingerprint(p)),
                    o.issues.empty() ? "valid" : o.issues.front().c_str());
    }

    // Prints the stages of one level that took at least 0.5 s, slowest first.
    void printStages(const services::LevelOutcome& o) {
        std::vector<core::planner::StageTime> stages = o.result.stages;
        std::ranges::sort(stages, [](const auto& a, const auto& b) { return a.seconds > b.seconds; });
        std::printf("           ");
        for (const auto& s : stages) {
            if (s.seconds >= 0.5) {
                std::printf(" %s %.1f s;", s.name.c_str(), s.seconds);
            }
        }
        std::printf("\n");
    }

    // Whether two outcomes hold the same plans.
    bool samePlans(const services::PlanOutcome& a, const services::PlanOutcome& b) {
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& x = a.levels[i].result.plan;
            const auto& y = b.levels[i].result.plan;
            if (x.runs != y.runs || x.moves.size() != y.moves.size() || x.lowerBound != y.lowerBound) {
                return false;
            }
        }
        return true;
    }

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // Lines appear as they are printed, also through a pipe.
    const Args a = parse(argc, argv);
    services::ThreadPool pool(std::max<std::size_t>(a.threads, 1));
    auto start = Clock::now();
    core::SynthOptions o;
    o.geometry = {8192, (a.terabytes << 40U) / 8192}; // NTFS needs 8 KB clusters above 16 TB.
    o.files = a.files;
    o.fragmentedShare = 0.3;
    o.maxFragments = 40;
    auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(o));
    std::printf("synthetic volume: %s files, %s extents, %s free runs in %.1f s\n",
                core::formatCount(snapshot->files.size()).c_str(), core::formatCount(snapshot->extents.size()).c_str(),
                core::formatCount(snapshot->freeSpace.size()).c_str(), since(start));
    services::PlanRequest r;
    r.script = mixed();
    r.now = 133'000'000'000'000'000LL;
    start = Clock::now();
    const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
    r.classes = engine.classify(*snapshot, {.now = r.now, .churn = {}}, pool);
    std::printf("classified in %.1f s\n", since(start));
    r.snapshot = std::move(snapshot);
    r.validate = a.validate;
    const services::PlanOutcome outcome = services::planLevels(r, pool);
    std::printf("planned three levels on %zu workers in %.1f s (target 60 s on 8)\n", a.threads, outcome.seconds);
    const char* names[] = {"fast", "moderate", "maximum"};
    for (std::size_t i = 0; i < 3; ++i) {
        printLevel(names[i], outcome.levels[i], outcome.bytesPerCluster);
        printStages(outcome.levels[i]);
    }
    if (a.compare) {
        core::InlineExecutor serial;
        const services::PlanOutcome one = services::planLevels(r, serial);
        std::printf("one worker: %.1f s (%.1fx), plans %s\n", one.seconds, one.seconds / outcome.seconds,
                    samePlans(one, outcome) ? "identical" : "DIFFER");
    }
    return 0;
}
