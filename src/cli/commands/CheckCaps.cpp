#include "cli/commands/CheckCaps.h"

#include "cli/commands/CliCommon.h"
#include "core/job/CancelToken.h"
#include "platform/win/Console.h"
#include "services/scan/AnalysisJob.h"
#include "services/scan/ScanCompare.h"

#include <cstdio>
#include <cwchar>
#include <optional>
#include <string>

namespace icd::cli {

    namespace {

        // Parsed arguments.
        struct CapsArgs {
            std::wstring volume; // As given.
            std::size_t threads = 2; // Workers of the capped run.
            std::uint64_t memoryBudget = std::uint64_t{2} << 30U; // Budget of the capped run.
        };

        // Applies the option at args[i] with its value at args[i + 1] ("--threads N", "--memory-budget BYTES"); false
        // when the option is unknown or the value missing or not a positive number.
        bool parseOption(std::span<const std::wstring_view> args, std::size_t i, CapsArgs& out) {
            if (i + 1 >= args.size()) {
                return false;
            }
            const std::uint64_t number = std::wcstoull(std::wstring(args[i + 1]).c_str(), nullptr, 10);
            if (args[i] == L"--threads") {
                out.threads = static_cast<std::size_t>(number);
            }
            else if (args[i] == L"--memory-budget") {
                out.memoryBudget = number;
            }
            else {
                return false;
            }
            return number > 0;
        }

        // Reads the arguments; nullopt when they are wrong.
        std::optional<CapsArgs> parse(std::span<const std::wstring_view> args) {
            CapsArgs out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (args[i].starts_with(L"--")) {
                    if (!parseOption(args, i, out)) {
                        return std::nullopt;
                    }
                    ++i;
                }
                else if (out.volume.empty()) {
                    out.volume = args[i];
                }
                else {
                    return std::nullopt;
                }
            }
            return out.volume.empty() ? std::nullopt : std::optional(out);
        }

        // One full MFT scan with an executor and memory settings; nothing is read from or written to the store.
        services::AnalysisResult scan(const core::DriveInfo& drive, core::Executor& executor, std::uint64_t budget,
                                      bool mapped, const core::CancelToken& token) {
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            services::JobContext context{token, progress, executor};
            services::AnalysisOptions options;
            options.scanner = services::ScannerKind::Mft;
            options.fallbackToApi = false;
            options.useSavedSnapshot = false;
            options.saveSnapshot = false;
            options.memoryBudgetBytes = budget;
            options.forceMappedStorage = mapped;
            auto result = services::analyzeVolume(drive, context, {}, options);
            ConsoleProgress::finish();
            return result;
        }

        // Both scans and the comparison; true when identical.
        bool check(services::AppServices& app, const core::DriveInfo& drive, const CapsArgs& args,
                   const core::CancelToken& token) {
            const services::AnalysisResult free = scan(drive, app.pool(), 0, false, token);
            std::printf("Uncapped: %zu workers, %s, files in %.2f s\n", app.pool().concurrency(),
                        free.memoryPlan.c_str(), free.scanSeconds);
            const auto own = ownExecutor(args.threads);
            core::Executor& executor = own ? *own : app.pool();
            const services::AnalysisResult capped = scan(drive, executor, args.memoryBudget, true, token);
            std::printf("Capped:   %zu workers, %s, files in %.2f s\n", executor.concurrency(),
                        capped.memoryPlan.c_str(), capped.scanSeconds);
            const services::ScanComparison c = services::compareExact(free.snapshot, capped.snapshot);
            std::printf("Capped against uncapped: %s (%llu files, %llu extents, %llu differences)\n",
                        c.same() ? "same" : "DIFFERENT", static_cast<unsigned long long>(c.filesCompared),
                        static_cast<unsigned long long>(c.runsCompared),
                        static_cast<unsigned long long>(c.differences));
            for (const std::string& line : c.samples) {
                std::printf("    %s\n", line.c_str());
            }
            return c.same();
        }

    } // namespace

    const char* checkCapsUsage() { return "icd-cli check-caps <volume> [--threads N] [--memory-budget BYTES]"; }

    int runCheckCaps(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", checkCapsUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const auto drive = findDrive(app, normalizeRoot(parsed->volume));
        const char* why = refusal(drive);
        if (why != nullptr || !drive) { // refusal() covers a missing drive; the check keeps the access below safe.
            std::fprintf(stderr, "icd-cli check-caps: %s\n", why != nullptr ? why : "no drive");
            return 1;
        }
        try {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            const bool same = check(app, *drive, *parsed, cancel.token());
            std::printf("%s\n", same ? "Caps change nothing." : "Caps CHANGE the result.");
            return same ? 0 : 1;
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli check-caps: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
