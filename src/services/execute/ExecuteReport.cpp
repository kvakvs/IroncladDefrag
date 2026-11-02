#include "services/execute/ExecuteReport.h"

#include "core/execute/PlanReplay.h"
#include "core/format/Json.h"
#include "core/log/Log.h"
#include "services/scan/AnalysisJob.h"

#include <array>
#include <format>
#include <unordered_map>

namespace icd::services {

    namespace {

        using core::ClusterRun;
        using core::JsonWriter;
        using core::execute::ExecutionPlan;

        // Stable names of the effort levels in reports.
        constexpr std::array<std::string_view, 3> levelNames{"fast", "moderate", "maximum"};

        // Samples kept per list.
        constexpr std::size_t sampleLimit = 20;

        // Runs as text: "[start+length, ...]".
        std::string runsText(std::span<const ClusterRun> runs) {
            std::string out = "[";
            for (const ClusterRun& r : runs) {
                out += std::format("{}{}+{}", out.size() > 1 ? ", " : "", r.start, r.length);
            }
            return out + "]";
        }

        // Per file: the index of its last record that touched the disk.
        std::unordered_map<std::uint64_t, const MoveRecord*> lastRecords(const ExecutionPlan& plan,
                                                                         std::span<const MoveRecord> records) {
            std::unordered_map<std::uint64_t, const MoveRecord*> out;
            for (const MoveRecord& r : records) {
                if (r.outcome != MoveOutcome::Skipped && !r.runsAfter.empty()) {
                    out[plan.moves[r.move].fileReference] = &r;
                }
            }
            return out;
        }

        // Where a file must lie after its last record: the planned destination when that move completed there, else
        // where the executor read it back.
        std::vector<ClusterRun> predicted(const ExecutionPlan& plan, const MoveRecord& r) {
            if (r.outcome == MoveOutcome::Moved && !r.replaced) {
                return core::execute::lcnRunsOf(core::execute::asStreamRuns(plan.destinations(plan.moves[r.move])));
            }
            return core::execute::lcnRunsOf(r.runsAfter);
        }

        // Writes the plan section.
        void writePlan(JsonWriter& json, const ExecutionRequest& request) {
            const auto& id = request.plan.identity;
            json.key("plan").beginObject();
            json.key("strategy").value(id.strategy);
            json.key("level").value(levelNames.at(static_cast<std::size_t>(id.effort)));
            json.key("moves").value(std::uint64_t{request.plan.moves.size()});
            json.key("movesTried").value(std::uint64_t{request.order.size()});
            json.key("clusters").value(core::execute::plannedClusters(request.plan));
            json.key("bytesPerCluster").value(std::uint64_t{id.geometry.bytesPerCluster});
            json.key("resumed").value(request.resume).endObject();
        }

        // Writes the revalidation section with every drop.
        void writeRevalidation(JsonWriter& json, const ExecutionRequest& request, const Revalidation& v) {
            const auto& r = v.replay;
            json.key("revalidation").beginObject();
            json.key("caughtUp").value(v.caughtUp).key("how").value(v.how).key("seconds").value(v.seconds);
            json.key("kept").value(std::uint64_t{r.kept.size()}).key("dropped").value(std::uint64_t{r.dropped.size()});
            json.key("alreadyDone").value(r.alreadyDone).key("partlyDone").value(r.partlyDone);
            json.key("pendingClusters").value(r.pendingClusters).key("drops").beginArray();
            for (const auto& d : r.dropped) {
                json.beginObject().key("index").value(std::uint64_t{d.move});
                json.key("path").value(request.plan.paths[d.move]);
                json.key("reason").value(core::execute::dropKey(d.reason)).endObject();
            }
            json.endArray().endObject();
        }

        // Writes the execution totals and every move that did not complete.
        void writeExecution(JsonWriter& json, const ExecutionRequest& request, const ExecutionReport& report) {
            const ExecutionTotals& t = report.totals;
            json.key("execution").beginObject();
            json.key("cancelled").value(report.cancelled).key("seconds").value(report.seconds);
            json.key("moved").value(t.moved).key("alreadyDone").value(t.alreadyDone).key("partial").value(t.partial);
            json.key("skipped")
                .value(t.skipped)
                .key("replaced")
                .value(t.replaced)
                .key("unverified")
                .value(t.unverified);
            json.key("clustersMoved").value(t.clustersMoved).key("problems").beginArray();
            for (const MoveRecord& r : report.records) {
                if (r.verified && (r.outcome == MoveOutcome::Moved || r.outcome == MoveOutcome::AlreadyDone)) {
                    continue;
                }
                json.beginObject()
                    .key("index")
                    .value(std::uint64_t{r.move})
                    .key("path")
                    .value(request.plan.paths[r.move]);
                json.key("outcome").value(outcomeKey(r.outcome)).key("reason").value(r.reason);
                json.key("verified").value(r.verified).key("error").value(std::uint64_t{r.win32Error}).endObject();
            }
            json.endArray().endObject();
        }

        // Writes runs as [[start, length], ...].
        void writeRuns(JsonWriter& json, std::span<const ClusterRun> runs) {
            json.beginArray();
            for (const ClusterRun& r : runs) {
                json.beginArray().value(r.start).value(r.length).endArray();
            }
            json.endArray();
        }

        // Writes every record.
        void writeRecords(JsonWriter& json, const ExecutionRequest& request, const ExecutionReport& report) {
            json.key("records").beginArray();
            for (const MoveRecord& r : report.records) {
                const auto& m = request.plan.moves[r.move];
                json.beginObject()
                    .key("index")
                    .value(std::uint64_t{r.move})
                    .key("path")
                    .value(request.plan.paths[r.move]);
                json.key("outcome").value(outcomeKey(r.outcome)).key("reason").value(r.reason);
                json.key("replaced").value(r.replaced).key("verified").value(r.verified);
                json.key("clusters").value(r.clustersMoved).key("destinations");
                writeRuns(json, request.plan.destinations(m));
                json.key("runsAfter");
                writeRuns(json, core::execute::lcnRunsOf(r.runsAfter));
                json.endObject();
            }
            json.endArray();
        }

        // Writes the rescan check.
        void writeRescan(JsonWriter& json, const RescanCheck& c) {
            json.key("rescan").beginObject();
            json.key("filesChecked").value(c.filesChecked).key("mismatches").value(c.mismatches).key("samples");
            json.beginArray();
            for (const auto& s : c.samples) {
                json.value(s);
            }
            json.endArray().endObject();
        }

        // Writes the zones.
        void writeZones(JsonWriter& json, std::span<const ZoneSpan> zones) {
            json.key("zones").beginArray();
            for (const ZoneSpan& z : zones) {
                json.beginObject().key("name").value(z.name).key("pieces");
                writeRuns(json, z.pieces);
                json.key("gap");
                writeRuns(json, z.gap);
                json.endObject();
            }
            json.endArray();
        }

    } // namespace

    RescanCheck checkAgainstRescan(const ExecutionRequest& request, const ExecutionOutcome& outcome,
                                   JobContext& context) {
        AnalysisOptions full;
        full.useSavedSnapshot = false;
        full.saveSnapshot = false;
        const AnalysisResult fresh = analyzeVolume(request.drive, context, {}, full);
        const core::execute::SnapshotFiles files(fresh.snapshot, request.plan);
        RescanCheck out;
        for (const auto& [reference, record] : lastRecords(request.plan, outcome.report.records)) {
            ++out.filesChecked;
            const auto now = files.runsOf(reference);
            const auto actual = now ? core::execute::lcnRunsOf(*now) : std::vector<ClusterRun>{};
            const auto expected = predicted(request.plan, *record);
            if (actual != expected) {
                ++out.mismatches;
                if (out.samples.size() < sampleLimit) {
                    out.samples.push_back(std::format("{}: predicted {}, rescan {}", request.plan.paths[record->move],
                                                      runsText(expected), runsText(actual)));
                }
            }
        }
        core::logging::info("rescan check", {{"files", out.filesChecked}, {"mismatches", out.mismatches}});
        return out;
    }

    std::vector<ZoneSpan> zoneSpans(const PlanOutcome& outcome, const PlanRequest& request,
                                    core::strategy::Effort level) {
        std::vector<ZoneSpan> out;
        for (const auto& z : outcome.levels.at(static_cast<std::size_t>(level)).result.layout.zones) {
            const bool named = z.zone < request.script.zones.size();
            out.push_back({named ? request.script.zones[z.zone].name : std::string(), z.pieces, z.gapPieces});
        }
        return out;
    }

    std::string executeJson(const ExecuteReportInput& input) {
        const ExecutionRequest& request = *input.request;
        const ExecutionOutcome& outcome = *input.outcome;
        JsonWriter json;
        json.beginObject().key("volume").value(request.drive.root).key("log").value(outcome.log.string());
        json.key("refused");
        if (outcome.revalidation.refused) {
            json.value(*outcome.revalidation.refused);
        }
        else {
            json.null();
        }
        writePlan(json, request);
        writeRevalidation(json, request, outcome.revalidation);
        writeExecution(json, request, outcome.report);
        json.key("snapshot").beginObject().key("saved").value(outcome.snapshotSaved);
        json.key("note").value(outcome.snapshotNote).endObject();
        if (input.rescan != nullptr) {
            writeRescan(json, *input.rescan);
        }
        writeZones(json, input.zones);
        if (input.records) {
            writeRecords(json, request, outcome.report);
        }
        json.endObject();
        return json.str() + "\n";
    }

    std::string executeText(const ExecuteReportInput& input) {
        const ExecutionRequest& request = *input.request;
        const ExecutionOutcome& outcome = *input.outcome;
        if (outcome.revalidation.refused) {
            return "Refused: " + *outcome.revalidation.refused + "\n";
        }
        const auto& v = outcome.revalidation;
        const ExecutionTotals& t = outcome.report.totals;
        std::string out =
            std::format("Volume {}  plan {} at {}, {} moves\n", request.drive.root, request.plan.identity.strategy,
                        levelNames.at(static_cast<std::size_t>(request.plan.identity.effort)), request.order.size());
        out += std::format("Revalidation: {} kept, {} dropped, {} already done ({}), {:.2f} s\n", v.replay.kept.size(),
                           v.replay.dropped.size(), v.replay.alreadyDone, v.how, v.seconds);
        out += std::format("Execution: {} moved, {} already done, {} partial, {} skipped, {} replaced, {} unverified, "
                           "{} clusters in {:.2f} s{}\n",
                           t.moved, t.alreadyDone, t.partial, t.skipped, t.replaced, t.unverified, t.clustersMoved,
                           outcome.report.seconds, outcome.report.cancelled ? " (cancelled)" : "");
        out += "Snapshot: " + outcome.snapshotNote + "\nLog: " + outcome.log.string() + "\n";
        if (input.rescan != nullptr) {
            out += std::format("Rescan: {} files checked, {} not where predicted\n", input.rescan->filesChecked,
                               input.rescan->mismatches);
        }
        return out;
    }

} // namespace icd::services
