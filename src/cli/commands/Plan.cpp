#include "cli/commands/Plan.h"

#include "cli/commands/CliCommon.h"
#include "core/classify/Catalog.h"
#include "core/classify/RuleEngine.h"
#include "core/job/CancelToken.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "platform/win/Console.h"
#include "platform/win/TextConvert.h"
#include "services/AppServices.h"
#include "services/execute/Execution.h"
#include "services/execute/ExecutionLog.h"
#include "services/plan/PlanJob.h"
#include "services/plan/PlanReport.h"
#include "services/scan/AnalysisJob.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace icd::cli {

    namespace {

        // Parsed arguments.
        struct PlanArgs {
            std::wstring volume; // As given: "T", "T:", "T:\" or a mount folder.
            std::string strategy = "mixed"; // Preset id.
            std::optional<core::strategy::Effort> level; // One level, or all.
            bool json = false; // Machine-readable output.
            bool moves = false; // Every move.
            bool blockUnaligned = false; // With shadow copies: leave out moves not aligned to their blocks.
            std::size_t threads = 0; // Own pool of N workers; 0 uses the app's pool.
            std::uint32_t synthetic = 0; // Files of a synthetic volume instead of a real one; 0: real.
            std::wstring save; // Where to save the chosen level's plan for icd-cli execute --plan; empty: nowhere.
        };

        // An option and its value on the command line.
        struct OptionValue {
            std::wstring_view option; // "--level".
            std::wstring_view value; // "fast".
        };

        // Applies an option with a value; false when it is bad.
        bool parseValue(OptionValue given, PlanArgs& out) {
            const std::wstring_view option = given.option;
            const std::wstring_view value = given.value;
            const auto number = std::wcstoull(std::wstring(value).c_str(), nullptr, 10);
            if (option == L"--strategy") {
                out.strategy = platform::toUtf8(std::wstring(value));
                return true;
            }
            if (option == L"--level") {
                out.level = effortNamed(value);
                return out.level.has_value();
            }
            if (option == L"--save") {
                out.save = value;
                return true;
            }
            if (option == L"--threads") {
                out.threads = static_cast<std::size_t>(number);
                return number > 0;
            }
            out.synthetic = static_cast<std::uint32_t>(number);
            return number > 0;
        }

        // Applies a flag or the volume; false when it is unknown.
        bool parseFlag(std::wstring_view arg, PlanArgs& out) {
            if (arg == L"--json" || arg == L"--moves") {
                (arg == L"--json" ? out.json : out.moves) = true;
                return true;
            }
            if (arg == L"--block-unaligned") {
                out.blockUnaligned = true;
                return true;
            }
            if (!arg.starts_with(L"--") && out.volume.empty()) {
                out.volume = arg;
                return true;
            }
            return false;
        }

        // Whether an option takes a value.
        bool takesValue(std::wstring_view arg) {
            return arg == L"--strategy" || arg == L"--level" || arg == L"--threads" || arg == L"--synthetic" ||
                arg == L"--save";
        }

        // Whether parsed arguments name something to plan, and --save comes with --level for a real volume.
        bool complete(const PlanArgs& out) {
            const bool saveable = out.save.empty() || (out.level && out.synthetic == 0);
            return (!out.volume.empty() || out.synthetic != 0) && saveable;
        }

        // Reads the arguments; nullopt on unknown options or a missing volume.
        std::optional<PlanArgs> parse(std::span<const std::wstring_view> args) {
            PlanArgs out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const bool withValue = takesValue(args[i]);
                const bool ok = withValue ? i + 1 < args.size() && parseValue({args[i], args[i + 1]}, out)
                                          : parseFlag(args[i], out);
                if (!ok) {
                    return std::nullopt;
                }
                i += withValue ? 1 : 0;
            }
            return complete(out) ? std::optional(out) : std::nullopt;
        }

        // A request for a seeded synthetic volume.
        services::PlanRequest syntheticRequest(std::uint32_t files, const core::strategy::ZoneScript& script,
                                               core::Executor& executor) {
            core::SynthOptions o;
            o.files = files;
            o.geometry.totalClusters = std::max<std::uint64_t>(1ULL << 20U, std::uint64_t{files} * 1024);
            o.fragmentedShare = 0.3;
            auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(o));
            services::PlanRequest r;
            r.script = script;
            r.now = services::fileTimeNow();
            const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
            r.classes = engine.classify(*snapshot, {.now = r.now, .churn = {}}, executor);
            r.snapshot = std::move(snapshot);
            return r;
        }

        // A request for a real volume: analysis first (saved snapshot caught up when possible).
        services::PlanRequest volumeRequest(const core::DriveInfo& drive, const core::strategy::ZoneScript& script,
                                            services::JobContext& context) {
            services::AnalysisResult result = services::analyzeVolume(drive, context, {}, {});
            auto snapshot = std::make_shared<core::Snapshot>(std::move(result.snapshot));
            return services::makePlanRequest(drive, std::move(snapshot), std::move(result.classes), script);
        }

        // Prints the report; 1 when a plan failed validation.
        int print(const services::PlanOutcome& outcome, const services::PlanRequest& request, const PlanArgs& args) {
            const services::PlanReportOptions options{.level = args.level, .moves = args.moves};
            const std::string out = args.json ? services::planJson(outcome, request, options)
                                              : services::planText(outcome, request, options);
            platform::useUtf8ConsoleOutput();
            std::fwrite(out.data(), 1, out.size(), stdout);
            const bool valid = std::ranges::all_of(outcome.levels, [](const auto& l) { return l.issues.empty(); });
            return valid ? 0 : 1;
        }

        // Saves the chosen level for icd-cli execute --plan (the plan part of an execution log).
        void save(const PlanArgs& args, const services::PlanOutcome& outcome, const services::PlanRequest& request,
                  const core::DriveInfo& drive) {
            const services::ExecutionLogWriter file(std::filesystem::path(args.save),
                                                    services::executionPlanFor(outcome, request, drive, *args.level));
            std::fprintf(stderr, "plan saved to %ls\n", args.save.c_str());
        }

        // Plans and prints; throws on failure.
        int plan(const PlanArgs& args, const core::strategy::ZoneScript& script) {
            services::AppServices app(args.synthetic != 0 ? services::DriveSource::Fake
                                                          : services::DriveSource::System);
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            const auto own = ownExecutor(args.threads);
            core::Executor& executor = own ? *own : app.pool();
            services::JobContext context{cancel.token(), progress, executor};
            struct EndLine {
                ~EndLine() { ConsoleProgress::finish(); }
            } endLine;
            std::optional<services::PlanRequest> request;
            std::optional<core::DriveInfo> drive;
            if (args.synthetic != 0) {
                request = syntheticRequest(args.synthetic, script, executor);
            }
            else {
                drive = findDrive(app, normalizeRoot(args.volume));
                if (const char* why = refusal(drive)) {
                    std::fprintf(stderr, "icd-cli plan: %s\n", why);
                    return 1;
                }
                request = volumeRequest(*drive, script, context);
                request->blockUnaligned = args.blockUnaligned;
            }
            const services::PlanOutcome outcome = services::planLevels(*request, executor, cancel.token(), &progress);
            ConsoleProgress::finish();
            if (!args.save.empty()) {
                save(args, outcome, *request, *drive);
            }
            return print(outcome, *request, args);
        }

    } // namespace

    const char* planUsage() {
        return "icd-cli plan <volume> [--strategy ID] [--level fast|moderate|max] [--json] [--moves] [--threads N] "
               "[--synthetic FILES] [--save FILE (with --level)] [--block-unaligned]";
    }

    int runPlan(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", planUsage());
            return 2;
        }
        const core::strategy::ZoneScript* script = presetNamed(parsed->strategy);
        if (script == nullptr) {
            std::fprintf(stderr, "icd-cli plan: unknown strategy \"%s\" (use %s)\n", parsed->strategy.c_str(),
                         presetIds().c_str());
            return 2;
        }
        try {
            return plan(*parsed, *script);
        }
        catch (const core::OperationCancelled&) {
            std::fprintf(stderr, "icd-cli plan: cancelled\n");
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli plan: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
