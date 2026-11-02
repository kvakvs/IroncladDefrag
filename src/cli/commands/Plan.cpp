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
#include "services/plan/PlanJob.h"
#include "services/plan/PlanReport.h"
#include "services/scan/AnalysisJob.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
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
            std::size_t threads = 0; // Own pool of N workers; 0 uses the app's pool.
            std::uint32_t synthetic = 0; // Files of a synthetic volume instead of a real one; 0: real.
        };

        // The level named on the command line.
        std::optional<core::strategy::Effort> levelNamed(std::wstring_view name) {
            if (name == L"fast") {
                return core::strategy::Effort::Fast;
            }
            if (name == L"moderate") {
                return core::strategy::Effort::Moderate;
            }
            if (name == L"max" || name == L"maximum") {
                return core::strategy::Effort::Maximum;
            }
            return std::nullopt;
        }

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
                out.level = levelNamed(value);
                return out.level.has_value();
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
            if (!arg.starts_with(L"--") && out.volume.empty()) {
                out.volume = arg;
                return true;
            }
            return false;
        }

        // Whether an option takes a value.
        bool takesValue(std::wstring_view arg) {
            return arg == L"--strategy" || arg == L"--level" || arg == L"--threads" || arg == L"--synthetic";
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
            return out.volume.empty() && out.synthetic == 0 ? std::nullopt : std::optional(out);
        }

        // The built-in preset named by the arguments, or null.
        const core::strategy::ZoneScript* presetNamed(const std::string& id) {
            const auto index = core::strategy::builtinPresetIndex(id);
            return index ? &core::strategy::builtinPresets()[*index] : nullptr;
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
            if (args.synthetic != 0) {
                request = syntheticRequest(args.synthetic, script, executor);
            }
            else {
                const auto drive = findDrive(app, normalizeRoot(args.volume));
                if (const char* why = refusal(drive)) {
                    std::fprintf(stderr, "icd-cli plan: %s\n", why);
                    return 1;
                }
                request = volumeRequest(*drive, script, context);
            }
            const services::PlanOutcome outcome = services::planLevels(*request, executor, cancel.token(), &progress);
            ConsoleProgress::finish();
            return print(outcome, *request, args);
        }

    } // namespace

    const char* planUsage() {
        return "icd-cli plan <volume> [--strategy ID] [--level fast|moderate|max] [--json] [--moves] [--threads N] "
               "[--synthetic FILES]";
    }

    int runPlan(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", planUsage());
            return 2;
        }
        const core::strategy::ZoneScript* script = presetNamed(parsed->strategy);
        if (script == nullptr) {
            std::string ids;
            for (const auto& p : core::strategy::builtinPresets()) {
                ids += (ids.empty() ? "" : ", ") + p.id;
            }
            std::fprintf(stderr, "icd-cli plan: unknown strategy \"%s\" (use %s)\n", parsed->strategy.c_str(),
                         ids.c_str());
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
