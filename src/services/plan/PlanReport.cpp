#include "services/plan/PlanReport.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/Duration.h"
#include "core/format/Json.h"

#include <chrono>
#include <cmath>
#include <format>

namespace icd::services {

    namespace {

        using core::planner::Plan;
        using core::strategy::Effort;

        // Planned clusters over what the target needs of them (1.0 when nothing had to move).
        double boundRatio(const Plan& p) {
            const auto reached = p.lowerBound - std::min(p.lowerBound, p.skippedClusters + p.deferredClusters);
            return reached == 0 ? 1.0 : static_cast<double>(p.totals.clustersMoved) / static_cast<double>(reached);
        }

        // Whether a level is reported.
        bool reported(const PlanReportOptions& options, std::size_t level) {
            return !options.level || static_cast<std::size_t>(*options.level) == level;
        }

        // Writes a run list.
        void writeRuns(core::JsonWriter& w, std::string_view key, std::span<const core::ClusterRun> runs) {
            w.key(key).beginArray();
            for (const core::ClusterRun& r : runs) {
                w.beginArray().value(r.start).value(r.length).endArray();
            }
            w.endArray();
        }

        // Writes the moves of a plan.
        void writeMoves(core::JsonWriter& w, const Plan& p, const PlanRequest& request) {
            w.key("moveList").beginArray();
            for (const auto& m : p.moves) {
                w.beginObject().key("file").value(request.snapshot->path(m.file));
                w.key("reason").value(core::planner::reasonKey(m.reason));
                const bool zoned = m.zone < request.script.zones.size();
                w.key("zone").value(zoned ? std::string_view(request.script.zones[m.zone].name) : std::string_view());
                w.key("extra").value(m.extra).key("fromParking").value(m.fromParking).key("clusters").value(m.clusters);
                writeRuns(w, "sources", p.sources(m));
                writeRuns(w, "destinations", p.destinations(m));
                w.endObject();
            }
            w.endArray();
        }

        // Writes the figures of one level.
        void writeMetrics(core::JsonWriter& w, const core::planner::PlanMetrics& m) {
            w.key("metrics").beginObject();
            w.key("fragmentedFilesPercentBefore").value(m.fragmentedBefore);
            w.key("fragmentedFilesPercentAfter").value(m.fragmentedAfter);
            w.key("zoneFitPercentBefore").value(m.zoneFitBefore);
            w.key("zoneFitPercentAfter").value(m.zoneFitAfter);
            w.key("freeRunsBefore").value(m.freeRunsBefore).key("freeRunsAfter").value(m.freeRunsAfter);
            w.key("largestFreeRunClustersAfter").value(m.largestFreeAfter);
            w.key("estimatedSeconds").value(m.seconds);
            w.endObject();
        }

        // Writes one level.
        void writeLevel(core::JsonWriter& w, const LevelOutcome& o, const PlanRequest& request,
                        const PlanReportOptions& options) {
            const Plan& p = o.result.plan;
            const std::uint64_t cluster = request.snapshot->geometry.bytesPerCluster;
            w.beginObject().key("level").value(core::strategy::effortKey(p.effort));
            w.key("moves").value(std::uint64_t{p.totals.moves}).key("files").value(std::uint64_t{p.totals.files});
            w.key("bytesMoved").value(p.totals.clustersMoved * cluster);
            w.key("lowerBoundBytes").value(p.lowerBound * cluster);
            w.key("ratioToLowerBound").value(boundRatio(p));
            w.key("extraBytes").value(p.extra * cluster);
            w.key("skippedBytes").value(p.skippedClusters * cluster);
            w.key("skippedFiles").value(std::uint64_t{p.skipped.size()});
            w.key("deferredBytes").value(p.deferredClusters * cluster);
            w.key("fragmentsBefore")
                .value(p.totals.fragmentsBefore)
                .key("fragmentsAfter")
                .value(p.totals.fragmentsAfter);
            writeMetrics(w, o.result.metrics);
            writeRuns(w, "hotSpots", o.result.hotSpots);
            w.key("notes").beginArray();
            for (const std::string& n : p.notes) {
                w.value(n);
            }
            w.endArray().key("valid").value(o.issues.empty()).key("issues").beginArray();
            for (const std::string& i : o.issues) {
                w.value(i);
            }
            w.endArray();
            if (options.moves) {
                writeMoves(w, p, request);
            }
            w.endObject();
        }

    } // namespace

    std::string planJson(const PlanOutcome& outcome, const PlanRequest& request, const PlanReportOptions& options) {
        core::JsonWriter w;
        w.beginObject();
        w.key("strategy").beginObject().key("id").value(request.script.id).key("name").value(request.script.name);
        w.endObject();
        w.key("bytesPerCluster").value(std::uint64_t{request.snapshot->geometry.bytesPerCluster});
        if (options.timing) {
            w.key("planningSeconds").value(outcome.seconds);
        }
        w.key("levels").beginArray();
        for (std::size_t level = 0; level < outcome.levels.size(); ++level) {
            if (reported(options, level)) {
                writeLevel(w, outcome.levels[level], request, options);
            }
        }
        w.endArray().endObject();
        return w.str() + "\n";
    }

    std::string planText(const PlanOutcome& outcome, const PlanRequest& request, const PlanReportOptions& options) {
        std::string out = std::format("Strategy: {} ({:.1f} s to plan)\n", request.script.name, outcome.seconds);
        const std::uint64_t cluster = request.snapshot->geometry.bytesPerCluster;
        for (std::size_t level = 0; level < outcome.levels.size(); ++level) {
            if (!reported(options, level)) {
                continue;
            }
            const auto& o = outcome.levels[level];
            const Plan& p = o.result.plan;
            const auto& m = o.result.metrics;
            out += std::format("\n{}: {} moves, {} files, {} (lower bound {}, ratio {:.3f}), about {}\n",
                               core::strategy::effortKey(p.effort), core::formatCount(p.totals.moves),
                               core::formatCount(p.totals.files), core::formatBytes(p.totals.clustersMoved * cluster),
                               core::formatBytes(p.lowerBound * cluster), boundRatio(p),
                               core::formatDuration(std::chrono::seconds(std::llround(m.seconds))));
            out += std::format(
                "  fragmented files {:.1f}% -> {:.1f}%, in target zone {:.1f}% -> {:.1f}%, free runs {} -> {}\n",
                m.fragmentedBefore, m.fragmentedAfter, m.zoneFitBefore, m.zoneFitAfter,
                core::formatCount(m.freeRunsBefore), core::formatCount(m.freeRunsAfter));
            out += std::format("  skipped {} ({} files), deferred {}, extra {}{}\n",
                               core::formatBytes(p.skippedClusters * cluster), core::formatCount(p.skipped.size()),
                               core::formatBytes(p.deferredClusters * cluster), core::formatBytes(p.extra * cluster),
                               o.issues.empty() ? "" : ", INVALID");
            for (const std::string& n : p.notes) {
                out += "  " + n + "\n";
            }
        }
        return out;
    }

} // namespace icd::services
