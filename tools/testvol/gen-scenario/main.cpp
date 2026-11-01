// gen-scenario: fills a test volume with one of the seeded scenarios of plan.md section 12. The same
// scenario, seed and volume size write the same files in the same order, so a fresh volume ends up with
// the same layout every time.
//
// Usage: gen-scenario <scenario> <drive> [--seed N] [--fill SHARE] [--force]
//   --fill   share of the volume used when done (default per scenario)
//   --force  allow a volume whose label does not start with ICD-TEST (never the Windows drive)

#include "FileOps.h"
#include "Scenarios.h"

#include "core/format/ByteSize.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

    using icd::testvol::Scenario;

    // Parsed command line.
    struct Options {
        const Scenario* scenario = nullptr; // Scenario to write.
        std::wstring root; // L"T:\\".
        std::uint64_t seed = 1; // Scenario seed.
        std::optional<double> fill; // Override of the scenario's fill level.
        bool force = false; // Skip the label check.
    };

    // Prints usage and the scenario list.
    void printUsage() {
        std::printf("Usage: gen-scenario <scenario> <drive> [--seed N] [--fill SHARE] [--force]\n\nScenarios:\n");
        for (const Scenario& s : icd::testvol::scenarios()) {
            std::printf("  %-12ls %.*s\n", std::wstring(s.name).c_str(), static_cast<int>(s.about.size()),
                        s.about.data());
        }
    }

    // Reads the options after the two positional arguments; throws on unknown ones.
    void parseFlags(int argc, wchar_t** argv, Options& o) {
        for (int i = 3; i < argc; ++i) {
            const std::wstring arg = argv[i];
            const bool hasValue = i + 1 < argc;
            if (arg == L"--seed" && hasValue) {
                o.seed = std::stoull(argv[++i]);
            }
            else if (arg == L"--fill" && hasValue) {
                o.fill = std::stod(argv[++i]);
            }
            else if (arg == L"--force") {
                o.force = true;
            }
            else {
                throw std::invalid_argument("unknown option");
            }
        }
    }

    // Parses the command line; nullopt means print usage.
    std::optional<Options> parse(int argc, wchar_t** argv) {
        if (argc < 3) {
            return std::nullopt;
        }
        Options o;
        o.scenario = icd::testvol::findScenario(argv[1]);
        const std::wstring drive = argv[2];
        if (o.scenario == nullptr || drive.empty() || std::iswalpha(drive[0]) == 0) {
            return std::nullopt;
        }
        o.root = std::wstring(1, static_cast<wchar_t>(std::towupper(drive[0]))) + L":\\";
        parseFlags(argc, argv, o);
        return o;
    }

    // Refuses the Windows drive always, and volumes not labelled ICD-TEST* unless forced: this tool
    // writes and deletes gigabytes.
    void checkTarget(const Options& o) {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        if (std::towupper(windows[0]) == o.root[0]) {
            throw std::runtime_error("refusing to write to the Windows drive");
        }
        wchar_t label[MAX_PATH + 1]{};
        wchar_t fs[MAX_PATH + 1]{};
        if (GetVolumeInformationW(o.root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH) == FALSE) {
            throw std::runtime_error("cannot read the volume information");
        }
        if (std::wstring(fs) != L"NTFS") {
            throw std::runtime_error("the volume is not NTFS");
        }
        if (!o.force && !std::wstring(label).starts_with(L"ICD-TEST")) {
            throw std::runtime_error("volume label does not start with ICD-TEST (use --force)");
        }
    }

    // Total and free bytes of the volume.
    std::pair<std::uint64_t, std::uint64_t> volumeSpace(const std::wstring& root) {
        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER total{};
        if (GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, &freeBytes) == FALSE) {
            throw std::runtime_error("cannot read free space");
        }
        return {total.QuadPart, freeBytes.QuadPart};
    }

    // Bytes the scenario may add: up to its fill level, or half the free space for fixed-size ones.
    std::uint64_t budgetFor(const Options& o) {
        const auto [total, freeBytes] = volumeSpace(o.root);
        const double fill = o.fill.value_or(o.scenario->fill);
        if (fill <= 0.0) {
            return freeBytes / 2;
        }
        const auto target = static_cast<std::uint64_t>(fill * static_cast<double>(total));
        const std::uint64_t used = total - freeBytes;
        return target > used ? target - used : 0;
    }

    // Writes the scenario and prints what it did.
    int run(const Options& o) {
        checkTarget(o);
        const auto start = std::chrono::steady_clock::now();
        icd::testvol::Generator gen(o.root, o.seed, budgetFor(o));
        const std::wstring dir(o.scenario->name);
        o.scenario->run(gen, dir, gen.budget());
        const auto stats = icd::testvol::countFragments(gen, dir);
        const auto [total, freeBytes] = volumeSpace(o.root);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto& t = gen.totals();
        std::printf("%ls seed %llu on %ls: wrote %llu files (%llu deleted), %s in %.1f s\n", dir.c_str(),
                    static_cast<unsigned long long>(o.seed), o.root.c_str(),
                    static_cast<unsigned long long>(t.filesWritten), static_cast<unsigned long long>(t.filesDeleted),
                    icd::core::formatBytes(t.bytesWritten).c_str(), seconds);
        std::printf(
            "  %llu files with clusters, %llu fragmented (%.1f%%), %llu fragments, most %llu\n",
            static_cast<unsigned long long>(stats.files), static_cast<unsigned long long>(stats.fragmented),
            stats.files == 0 ? 0.0 : 100.0 * static_cast<double>(stats.fragmented) / static_cast<double>(stats.files),
            static_cast<unsigned long long>(stats.fragments), static_cast<unsigned long long>(stats.maxFragments));
        std::printf("  volume %.1f%% used\n",
                    100.0 * static_cast<double>(total - freeBytes) / static_cast<double>(total));
        return 0;
    }

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const auto options = parse(argc, argv);
        if (!options) {
            printUsage();
            return 2;
        }
        return run(*options);
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "gen-scenario: %s\n", e.what());
        return 1;
    }
}
