#include "cli/commands/Execute.h"

#include "cli/commands/CliCommon.h"
#include "core/format/DriveText.h"
#include "core/job/CancelToken.h"
#include "core/job/PauseGate.h"
#include "platform/win/Console.h"
#include "platform/win/MoveFile.h"
#include "platform/win/RetrievalPointers.h"
#include "platform/win/TextConvert.h"
#include "services/AppServices.h"
#include "services/execute/ExecuteReport.h"
#include "services/execute/Execution.h"
#include "services/execute/ExecutionLog.h"
#include "services/plan/PlanJob.h"
#include "services/scan/AnalysisJob.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace icd::cli {

    namespace {

        // Parsed arguments.
        struct ExecuteArgs {
            std::wstring volume; // As given: "T", "T:", "T:\" or a mount folder.
            std::string strategy = "mixed"; // Preset id when planning now.
            core::strategy::Effort level = core::strategy::Effort::Fast; // Level when planning now.
            std::wstring planFile; // A plan saved by icd-cli plan --save; empty: plan now.
            std::wstring logFile; // The log to resume (--resume --log FILE); empty: the newest unfinished one.
            bool resume = false; // Resume an execution log.
            bool json = false; // Machine-readable output.
            bool moves = false; // Every record in the report.
            bool lowPriority = false; // Background I/O priority.
            bool noPrefetch = false; // No prefetch helper.
            bool checkRescan = false; // Scan again afterwards and compare.
            bool blockUnaligned = false; // When planning now with shadow copies: leave out unaligned moves.
            std::uint64_t chunkMb = 16; // Largest move call.
            std::uint64_t limitMbps = 0; // Throughput limit in MB/s; 0: none.
            std::uint64_t maxAgeHours = 24; // Oldest plan accepted.
            std::uint64_t threads = 0; // Own pool of N workers; 0 uses the app's pool.
            std::uint64_t cancelAfter = 0; // Test hook: cancel after N finished moves.
            std::uint64_t pauseAfter = 0; // Test hook: pause after N finished moves...
            std::uint64_t pauseMs = 500; // ...for this long.
            std::uint64_t maxMoves = 0; // Run only the first N moves left (tests, small trial runs); 0: all.
            std::optional<std::uint32_t> takeDestination; // Test hook: a stale bitmap just before this move.
        };

        // Flags and the field each sets.
        constexpr std::array<std::pair<std::wstring_view, bool ExecuteArgs::*>, 7> flags{{
            {L"--resume", &ExecuteArgs::resume},
            {L"--json", &ExecuteArgs::json},
            {L"--moves", &ExecuteArgs::moves},
            {L"--low-priority", &ExecuteArgs::lowPriority},
            {L"--no-prefetch", &ExecuteArgs::noPrefetch},
            {L"--check-rescan", &ExecuteArgs::checkRescan},
            {L"--block-unaligned", &ExecuteArgs::blockUnaligned},
        }};

        // Number options and the field each sets.
        constexpr std::array<std::pair<std::wstring_view, std::uint64_t ExecuteArgs::*>, 8> numbers{{
            {L"--chunk-mb", &ExecuteArgs::chunkMb},
            {L"--limit-mbps", &ExecuteArgs::limitMbps},
            {L"--max-age-hours", &ExecuteArgs::maxAgeHours},
            {L"--threads", &ExecuteArgs::threads},
            {L"--cancel-after", &ExecuteArgs::cancelAfter},
            {L"--pause-after", &ExecuteArgs::pauseAfter},
            {L"--pause-ms", &ExecuteArgs::pauseMs},
            {L"--max-moves", &ExecuteArgs::maxMoves},
        }};

        // An option and its value on the command line.
        struct OptionValue {
            std::wstring_view option; // "--level".
            std::wstring_view value; // "fast".
        };

        // Applies a text option; false when it is bad or not a text option.
        bool applyText(OptionValue given, ExecuteArgs& out) {
            if (given.option == L"--strategy") {
                out.strategy = platform::toUtf8(std::wstring(given.value));
                return true;
            }
            if (given.option == L"--level") {
                const auto level = effortNamed(given.value);
                out.level = level.value_or(out.level);
                return level.has_value();
            }
            if (given.option == L"--plan" || given.option == L"--log") {
                (given.option == L"--plan" ? out.planFile : out.logFile) = given.value;
                return !given.value.empty();
            }
            if (given.option == L"--take-destination") {
                out.takeDestination =
                    static_cast<std::uint32_t>(std::wcstoul(std::wstring(given.value).c_str(), nullptr, 10));
                return true;
            }
            return false;
        }

        // Applies an option with a value; false when it is unknown or bad.
        bool applyValue(OptionValue given, ExecuteArgs& out) {
            const auto number = std::ranges::find(numbers, given.option, &decltype(numbers)::value_type::first);
            if (number == numbers.end()) {
                return applyText(given, out);
            }
            wchar_t* end = nullptr;
            const std::wstring text(given.value);
            out.*(number->second) = std::wcstoull(text.c_str(), &end, 10);
            return end != text.c_str() && *end == L'\0';
        }

        // Whether an option takes a value.
        bool takesValue(std::wstring_view arg) {
            const bool text = arg == L"--strategy" || arg == L"--level" || arg == L"--plan" || arg == L"--log" ||
                arg == L"--take-destination";
            return text || std::ranges::find(numbers, arg, &decltype(numbers)::value_type::first) != numbers.end();
        }

        // Applies a flag or the volume; false when it is unknown.
        bool applyFlag(std::wstring_view arg, ExecuteArgs& out) {
            const auto flag = std::ranges::find(flags, arg, &decltype(flags)::value_type::first);
            if (flag != flags.end()) {
                out.*(flag->second) = true;
                return true;
            }
            if (!arg.starts_with(L"--") && out.volume.empty()) {
                out.volume = arg;
                return true;
            }
            return false;
        }

        // Whether parsed arguments are usable: a volume, a chunk size, not a plan and a resume at once.
        bool complete(const ExecuteArgs& out) {
            return !out.volume.empty() && out.chunkMb != 0 && !(out.resume && !out.planFile.empty());
        }

        // Reads the arguments; nullopt on unknown options, a missing volume or a plan and a resume at once.
        std::optional<ExecuteArgs> parse(std::span<const std::wstring_view> args) {
            ExecuteArgs out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const bool withValue = takesValue(args[i]);
                const bool ok = withValue ? i + 1 < args.size() && applyValue({args[i], args[i + 1]}, out)
                                          : applyFlag(args[i], out);
                if (!ok) {
                    return std::nullopt;
                }
                i += withValue ? 1 : 0;
            }
            return complete(out) ? std::optional(out) : std::nullopt;
        }

        // Another program takes cluster `lcn` (a stale bitmap, plan.md 10.4): a small probe file is written and its
        // first cluster moved there. False when the cluster is not free or the move fails.
        bool occupyCluster(const core::DriveInfo& drive, core::Lcn lcn, const std::filesystem::path& probe) {
            const auto volume = platform::openVolumeForMoves(platform::fromUtf8(drive.layout.volumeName));
            if (!volume || platform::freeRunsIn(volume.get(), {lcn, 1}).empty()) {
                return false;
            }
            {
                std::ofstream out(probe, std::ios::binary);
                const std::string block(std::size_t{64} << 10U, 'x');
                out.write(block.data(), static_cast<std::streamsize>(block.size()));
            }
            const auto file = platform::openForQuery(probe.wstring());
            return file &&
                platform::moveClusters({volume.get(), file.get()}, {0, lcn, 1}).error == platform::MoveError::None;
        }

        // The scripted test hooks of a run: pause, cancel and a stale bitmap at given points.
        class ScriptedRun {
        public:
            // Hooks for the arguments' test options.
            ScriptedRun(const ExecuteArgs& args, core::CancelSource& cancel, core::PauseGate& gate,
                        const core::DriveInfo& drive) :
                args_(args), cancel_(cancel), gate_(gate), drive_(drive),
                probe_(std::filesystem::path(platform::fromUtf8(drive.root)) / "icd-stale-probe.bin") {}

            ScriptedRun(const ScriptedRun&) = delete;
            ScriptedRun& operator=(const ScriptedRun&) = delete;
            ScriptedRun(ScriptedRun&&) = delete;
            ScriptedRun& operator=(ScriptedRun&&) = delete;

            // Removes the probe file if a run left one.
            ~ScriptedRun() { removeProbe(); }

            // Removes the probe file, so the volume holds only the test data again (before any rescan).
            void removeProbe() const {
                std::error_code ignored;
                std::filesystem::remove(probe_, ignored);
            }

            // The hooks, for a request whose plan they read.
            services::ExecutionHooks hooks(const services::ExecutionRequest& request) {
                services::ExecutionHooks h;
                h.onRecord = [this](const services::MoveRecord&) { recorded(); };
                h.onBegin = [this, &request](std::uint32_t move) { beginning(request, move); };
                return h;
            }

        private:
            // After each finished move: cancel or pause when the count is reached.
            void recorded() {
                ++records_;
                if (records_ == args_.cancelAfter) {
                    cancel_.cancel();
                }
                if (records_ == args_.pauseAfter) {
                    gate_.pause();
                    resumer_ = std::jthread([this] {
                        std::this_thread::sleep_for(std::chrono::milliseconds(args_.pauseMs));
                        gate_.resume();
                    });
                }
            }

            // Before a move: take its first destination cluster when asked to.
            void beginning(const services::ExecutionRequest& request, std::uint32_t move) {
                if (args_.takeDestination != move) {
                    return;
                }
                const auto destinations = request.plan.destinations(request.plan.moves[move]);
                const bool taken = !destinations.empty() && occupyCluster(drive_, destinations.front().start, probe_);
                std::fprintf(stderr, "\nstale bitmap before move %u: %s\n", move,
                             taken ? "destination taken" : "not taken");
            }

            const ExecuteArgs& args_; // The options.
            core::CancelSource& cancel_; // Cancels the run.
            core::PauseGate& gate_; // Pauses it.
            const core::DriveInfo& drive_; // The volume.
            std::filesystem::path probe_; // The file that takes a destination.
            std::uint64_t records_ = 0; // Moves finished so far.
            std::jthread resumer_; // Resumes after a scripted pause.
        };

        // A request and, when planned in this run, its zones.
        struct Prepared {
            services::ExecutionRequest request; // What to run.
            std::vector<services::ZoneSpan> zones; // The plan's zones (planned now only).
        };

        // Analyses and plans the volume now.
        Prepared planNow(const ExecuteArgs& args, const core::DriveInfo& drive, services::JobContext& context) {
            const core::strategy::ZoneScript* script = presetNamed(args.strategy);
            if (script == nullptr) {
                throw std::runtime_error("unknown strategy \"" + args.strategy + "\" (use " + presetIds() + ")");
            }
            services::AnalysisResult analysis = services::analyzeVolume(drive, context, {}, {});
            auto snapshot = std::make_shared<core::Snapshot>(std::move(analysis.snapshot));
            services::PlanRequest request =
                services::makePlanRequest(drive, std::move(snapshot), std::move(analysis.classes), *script);
            request.blockUnaligned = args.blockUnaligned;
            const services::PlanOutcome outcome =
                services::planLevels(request, context.executor, context.token, &context.progress);
            const auto& issues = outcome.levels.at(static_cast<std::size_t>(args.level)).issues;
            if (!issues.empty()) {
                throw std::runtime_error("the plan failed validation: " + issues.front());
            }
            return {services::freshExecution(drive, services::executionPlanFor(outcome, request, drive, args.level)),
                    services::zoneSpans(outcome, request, args.level)};
        }

        // The request the arguments ask for: a resume, a saved plan, or a plan made now.
        Prepared prepare(const ExecuteArgs& args, const core::DriveInfo& drive, services::JobContext& context) {
            if (args.resume) {
                auto log = args.logFile.empty()
                    ? services::unfinishedExecutionLog(services::executionLogDirectory(), drive.layout.volumeName)
                    : std::optional(std::filesystem::path(args.logFile));
                if (!log) {
                    throw std::runtime_error("no unfinished execution log for this volume");
                }
                return {services::resumedExecution(drive, *log), {}};
            }
            if (!args.planFile.empty()) {
                return {services::freshExecution(drive, services::readExecutionLog(args.planFile).plan), {}};
            }
            return planNow(args, drive, context);
        }

        // Applies the options to a request.
        void configure(const ExecuteArgs& args, services::ExecutionRequest& request) {
            request.options.chunkBytes = args.chunkMb << 20U;
            request.options.lowPriority = args.lowPriority;
            request.options.throughputLimit = args.limitMbps << 20U;
            request.options.prefetch = !args.noPrefetch;
            if (args.maxMoves != 0 && request.order.size() > args.maxMoves) {
                request.order.resize(args.maxMoves); // A prefix of a plan is valid on its own; the log keeps the rest.
            }
            if (!request.resume) {
                request.revalidate.maxAgeTicks =
                    static_cast<std::int64_t>(args.maxAgeHours) * services::fileTimeTicksPerHour;
            }
        }

        // Prints the report; the exit code.
        int print(const services::ExecuteReportInput& input, bool json) {
            const std::string out = json ? services::executeJson(input) : services::executeText(input);
            platform::useUtf8ConsoleOutput();
            std::fwrite(out.data(), 1, out.size(), stdout);
            if (input.outcome->revalidation.refused) {
                return 1;
            }
            return input.outcome->report.cancelled ? 3 : 0;
        }

        // Runs the command; throws on failure.
        int execute(const ExecuteArgs& args) {
            services::AppServices app(services::DriveSource::System);
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            const auto own = ownExecutor(args.threads);
            core::Executor& executor = own ? *own : app.pool();
            services::JobContext context{cancel.token(), progress, executor};
            const auto drive = findDrive(app, normalizeRoot(args.volume));
            if (const char* why = refusal(drive)) {
                std::fprintf(stderr, "icd-cli execute: %s\n", why);
                return 1;
            }
            if (const std::string blocked = core::movesBlockedText(*drive); !blocked.empty()) {
                std::fprintf(stderr, "icd-cli execute: %s\n", blocked.c_str());
                return 1;
            }
            Prepared prepared = prepare(args, *drive, context);
            configure(args, prepared.request);
            core::PauseGate gate;
            ScriptedRun script(args, cancel, gate, *drive);
            prepared.request.observers = script.hooks(prepared.request);
            const services::ExecutionOutcome outcome = services::runExecution(prepared.request, context, &gate);
            script.removeProbe();
            std::optional<services::RescanCheck> rescan;
            if (args.checkRescan && !outcome.revalidation.refused) {
                rescan = services::checkAgainstRescan(prepared.request, outcome, context);
            }
            ConsoleProgress::finish();
            return print({&prepared.request, &outcome, rescan ? &*rescan : nullptr, prepared.zones, args.moves},
                         args.json);
        }

    } // namespace

    const char* executeUsage() {
        return "icd-cli execute <volume> [--strategy ID] [--level fast|moderate|max] | [--plan FILE] | [--resume "
               "[--log FILE]] [--json] [--moves] [--chunk-mb N] [--low-priority] [--limit-mbps N] [--max-age-hours H] "
               "[--no-prefetch] [--check-rescan] [--threads N] [--pause-after N --pause-ms M] [--cancel-after N] "
               "[--take-destination MOVE] [--max-moves N] [--block-unaligned]";
    }

    int runExecute(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", executeUsage());
            return 2;
        }
        try {
            return execute(*parsed);
        }
        catch (const core::OperationCancelled&) {
            ConsoleProgress::finish();
            std::fprintf(stderr, "icd-cli execute: cancelled\n");
            return 3;
        }
        catch (const std::exception& e) {
            ConsoleProgress::finish();
            std::fprintf(stderr, "icd-cli execute: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
