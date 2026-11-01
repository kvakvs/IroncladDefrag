#include "cli/commands/CompareScanners.h"

#include "cli/commands/CliCommon.h"
#include "core/job/CancelToken.h"
#include "platform/win/Console.h"
#include "services/scan/AnalysisJob.h"
#include "services/scan/ScanCompare.h"

#include <cstdio>
#include <cwchar>
#include <optional>
#include <string>
#include <vector>

namespace icd::cli {

    namespace {

        // Parsed arguments.
        struct CompareArgs {
            std::wstring volume; // As given.
            std::vector<std::size_t> threads{1, 2, 0}; // Worker counts of the MFT runs; 0 is the app's pool.
        };

        // "1,2,8" as numbers; empty on a bad list.
        std::vector<std::size_t> parseThreads(std::wstring_view text) {
            std::vector<std::size_t> out;
            std::wstring list(text);
            wchar_t* at = list.data();
            while (*at != L'\0') {
                wchar_t* end = nullptr;
                out.push_back(static_cast<std::size_t>(std::wcstoul(at, &end, 10)));
                if (end == at) {
                    return {};
                }
                at = *end == L',' ? end + 1 : end;
            }
            return out;
        }

        // Reads the arguments; nullopt when they are wrong.
        std::optional<CompareArgs> parse(std::span<const std::wstring_view> args) {
            CompareArgs out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (args[i] == L"--threads" && i + 1 < args.size()) {
                    out.threads = parseThreads(args[++i]);
                }
                else if (!args[i].starts_with(L"--") && out.volume.empty()) {
                    out.volume = args[i];
                }
                else {
                    return std::nullopt;
                }
            }
            return out.volume.empty() || out.threads.empty() ? std::nullopt : std::optional(out);
        }

        // One analysis with a scanner and an executor.
        services::AnalysisResult scan(const core::DriveInfo& drive, services::ScannerKind kind,
                                      core::Executor& executor, const core::CancelToken& token) {
            core::ThrottledProgress progress(std::make_shared<ConsoleProgress>());
            services::JobContext context{token, progress, executor};
            services::AnalysisOptions options;
            options.scanner = kind;
            options.fallbackToApi = false;
            auto result = services::analyzeVolume(drive, context, {}, options);
            ConsoleProgress::finish();
            return result;
        }

        // Prints a comparison; true when it found nothing.
        bool print(const char* what, const services::ScanComparison& c) {
            std::printf("  %-34s %s (%llu files, %llu runs compared, %llu differences; %llu extra metadata files, %llu "
                        "deferred access times)\n",
                        what, c.same() ? "same" : "DIFFERENT", static_cast<unsigned long long>(c.filesCompared),
                        static_cast<unsigned long long>(c.runsCompared), static_cast<unsigned long long>(c.differences),
                        static_cast<unsigned long long>(c.extraMetadataFiles),
                        static_cast<unsigned long long>(c.deferredAccessTimes));
            for (const std::string& line : c.samples) {
                std::printf("      %s\n", line.c_str());
            }
            return c.same();
        }

        // Read figures of an MFT run.
        void printFigures(const services::AnalysisResult& r) {
            if (!r.scan.mft) {
                return;
            }
            const services::MftReadFigures& f = *r.scan.mft;
            std::printf(
                "      %llu of %llu records in use, %llu skipped, %llu bad, %llu extension records, %.1f MB read in "
                "%.2f s, disk busy %.1f%% (device %.1f%%), reader starved %.2f s\n",
                static_cast<unsigned long long>(f.recordsInUse), static_cast<unsigned long long>(f.records),
                static_cast<unsigned long long>(f.skippedRecords), static_cast<unsigned long long>(f.badRecords),
                static_cast<unsigned long long>(f.extensionRecords), static_cast<double>(f.bytesRead) / 1e6,
                f.readSeconds, f.readerBusyPercent, f.deviceBusyPercent, f.starvedSeconds);
        }

        // Runs every scan and comparison; true when all agree.
        bool compareAll(services::AppServices& app, const core::DriveInfo& drive, const CompareArgs& args,
                        const core::CancelToken& token) {
            const services::AnalysisResult api = scan(drive, services::ScannerKind::Api, app.pool(), token);
            std::printf("API scan: %llu files, %llu extents, %.2f s\n",
                        static_cast<unsigned long long>(api.snapshot.files.size()),
                        static_cast<unsigned long long>(api.snapshot.extents.size()), api.scanSeconds);
            bool same = true;
            std::optional<services::AnalysisResult> first;
            for (const std::size_t threads : args.threads) {
                const auto own = ownExecutor(threads);
                core::Executor& executor = own ? *own : app.pool();
                services::AnalysisResult mft = scan(drive, services::ScannerKind::Mft, executor, token);
                std::printf("MFT scan, %zu workers: %llu files, %llu extents, %.2f s (%.1fx the API scan)\n",
                            executor.concurrency(), static_cast<unsigned long long>(mft.snapshot.files.size()),
                            static_cast<unsigned long long>(mft.snapshot.extents.size()), mft.scanSeconds,
                            mft.scanSeconds > 0 ? api.scanSeconds / mft.scanSeconds : 0.0);
                printFigures(mft);
                same = print("against the API scan", services::compareWithApi(api.snapshot, api.scan, mft.snapshot)) &&
                    same;
                if (first) {
                    same = print("against the first MFT scan", services::compareExact(first->snapshot, mft.snapshot)) &&
                        same;
                }
                else {
                    first = std::move(mft);
                }
            }
            return same;
        }

    } // namespace

    const char* compareScannersUsage() { return "icd-cli compare-scanners <volume> [--threads 1,2,8]"; }

    int runCompareScanners(std::span<const std::wstring_view> args) {
        const auto parsed = parse(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", compareScannersUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const auto drive = findDrive(app, normalizeRoot(parsed->volume));
        if (const char* why = refusal(drive)) {
            std::fprintf(stderr, "icd-cli compare-scanners: %s\n", why);
            return 1;
        }
        try {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            const bool same = compareAll(app, *drive, *parsed, cancel.token());
            std::printf("%s\n", same ? "Scanners agree." : "Scanners DIFFER.");
            return same ? 0 : 1;
        }
        catch (const core::OperationCancelled&) {
            std::fprintf(stderr, "icd-cli compare-scanners: cancelled\n");
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli compare-scanners: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
