#include "cli/commands/Analyze.h"

#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "platform/win/Console.h"
#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"
#include "services/AppServices.h"
#include "services/scan/AnalysisJob.h"
#include "services/scan/AnalysisReport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
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
        };

        // Progress on stderr, overwriting one line, so stdout stays clean for JSON.
        class ConsoleProgress : public core::ProgressSink {
        public:
            void report(const core::Progress& p) override {
                if (p.total != 0) {
                    std::fprintf(stderr, "\r%-24s %5.1f%%   ", p.stage.c_str(), 100.0 * p.fraction());
                }
                else {
                    std::fprintf(stderr, "\r%-24s          ", p.stage.c_str());
                }
            }

            // Ends the progress line.
            static void finish() { std::fprintf(stderr, "\r%60s\r", ""); }
        };

        // Reads the arguments; nullopt on unknown options or a missing volume.
        std::optional<AnalyzeArgs> parse(std::span<const std::wstring_view> args) {
            AnalyzeArgs out;
            for (const std::wstring_view arg : args) {
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
                    return std::nullopt;
                }
            }
            return out.volume.empty() ? std::nullopt : std::optional(out);
        }

        // "t", "T:" and "T:\" all become "T:\"; other paths get a trailing backslash.
        std::string normalizeRoot(const std::wstring& volume) {
            std::wstring root = volume;
            if (root.size() <= 2 && std::iswalpha(root[0]) != 0) {
                root = std::wstring(1, static_cast<wchar_t>(std::towupper(root[0]))) + L":";
            }
            if (root.back() != L'\\') {
                root += L'\\';
            }
            return platform::toUtf8(root);
        }

        // The drive mounted at the root, or nullopt.
        std::optional<core::DriveInfo> findDrive(services::AppServices& app, const std::string& root) {
            for (core::DriveInfo& drive : app.drives().listDrives()) {
                const bool same = std::ranges::equal(drive.root, root, [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                });
                if (same) {
                    return std::move(drive);
                }
            }
            return std::nullopt;
        }

        // Explains why a drive cannot be analysed, or returns nullptr.
        const char* refusal(const std::optional<core::DriveInfo>& drive) {
            if (!drive) {
                return "no mounted volume at that path";
            }
            if (drive->fileSystem != "NTFS") {
                return "only NTFS volumes can be analysed";
            }
            if (!platform::isElevated()) {
                return "analyze needs administrator rights; run it from an elevated prompt";
            }
            return nullptr;
        }

        // Runs the analysis with console progress and Ctrl+C cancel.
        services::AnalysisResult analyze(services::AppServices& app, const core::DriveInfo& drive) {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            services::JobContext context{cancel.token(), progress, app.pool()};
            struct EndLine {
                ~EndLine() { ConsoleProgress::finish(); }
            } endLine;
            return services::analyzeVolume(drive, context);
        }

    } // namespace

    const char* analyzeUsage() { return "icd-cli analyze <volume> [--json] [--dump-extents]"; }

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
            const services::AnalysisResult result = analyze(app, *drive);
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
