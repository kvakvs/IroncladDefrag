#include "cli/commands/Analyze.h"

#include "cli/commands/CliCommon.h"
#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "platform/win/Console.h"
#include "platform/win/Volumes.h"
#include "services/AppServices.h"
#include "services/scan/AnalysisJob.h"
#include "services/scan/AnalysisReport.h"

#include <cstdio>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>

namespace icd::cli {

    namespace {

        // Parsed arguments.
        struct AnalyzeArgs {
            std::wstring volume; // As given: "T", "T:", "T:\" or a mount folder.
            bool json = false; // Machine-readable output.
            bool dumpExtents = false; // Every file's runs (implies json).
            std::optional<services::ScannerKind> scanner; // --scanner api|mft; default: MFT with API fallback.
            std::size_t threads = 0; // --threads N: own pool of N workers (1: inline); 0 uses the app's pool.
        };

        // Applies the option at args[i] with its value at args[i + 1] (--scanner, --threads); false when the value is
        // missing or bad.
        bool parseValue(std::span<const std::wstring_view> args, std::size_t i, AnalyzeArgs& out) {
            if (i + 1 >= args.size()) {
                return false;
            }
            const std::wstring_view option = args[i];
            const std::wstring_view value = args[i + 1];
            if (option == L"--scanner") {
                out.scanner = scannerNamed(value);
                return out.scanner.has_value();
            }
            if (option == L"--threads") {
                out.threads = static_cast<std::size_t>(std::wcstoul(std::wstring(value).c_str(), nullptr, 10));
                return out.threads > 0;
            }
            return false;
        }

        // Applies a flag or the volume; false when it is unknown.
        bool parseFlag(std::wstring_view arg, AnalyzeArgs& out) {
            if (arg == L"--json") {
                out.json = true;
            }
            else if (arg == L"--dump-extents") {
                out.dumpExtents = out.json = true;
            }
            else if (!arg.starts_with(L"--") && out.volume.empty()) {
                out.volume = arg;
            }
            else {
                return false;
            }
            return true;
        }

        // Reads the arguments; nullopt on unknown options or a missing volume.
        std::optional<AnalyzeArgs> parse(std::span<const std::wstring_view> args) {
            AnalyzeArgs out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const bool withValue = args[i] == L"--scanner" || args[i] == L"--threads";
                const bool ok = withValue ? parseValue(args, i, out) : parseFlag(args[i], out);
                if (!ok) {
                    return std::nullopt;
                }
                i += withValue ? 1 : 0;
            }
            return out.volume.empty() ? std::nullopt : std::optional(out);
        }

        // Runs the analysis with console progress and Ctrl+C cancel.
        services::AnalysisResult analyze(services::AppServices& app, const core::DriveInfo& drive,
                                         const AnalyzeArgs& args) {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            const auto own = ownExecutor(args.threads);
            services::JobContext context{cancel.token(), progress, own ? *own : app.pool()};
            struct EndLine {
                ~EndLine() { ConsoleProgress::finish(); }
            } endLine;
            services::AnalysisOptions options;
            if (args.scanner) {
                options.scanner = *args.scanner;
                options.fallbackToApi = false; // One scanner was asked for by name: failures are reported.
            }
            return services::analyzeVolume(drive, context, {}, options);
        }

    } // namespace

    const char* analyzeUsage() {
        return "icd-cli analyze <volume> [--json] [--dump-extents] [--scanner api|mft] [--threads N]";
    }

    int runAnalyze(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", analyzeUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const auto drive = findDrive(app, normalizeRoot(parsed->volume));
        if (const char* why = refusal(drive)) {
            std::fprintf(stderr, "icd-cli analyze: %s\n", why);
            return 1;
        }
        try {
            const services::AnalysisResult result = analyze(app, *drive, *parsed);
            const std::string out = parsed->json ? services::analysisJson(*drive, result, parsed->dumpExtents)
                                                 : services::analysisText(*drive, result);
            platform::useUtf8ConsoleOutput();
            std::fwrite(out.data(), 1, out.size(), stdout);
            return 0;
        }
        catch (const core::OperationCancelled&) {
            std::fprintf(stderr, "icd-cli analyze: cancelled\n");
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli analyze: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
