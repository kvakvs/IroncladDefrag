// make-scalevol: fills a (thin, VHDX) test volume with millions of files whose clusters are allocated but never written
// (plan.md section 12, scale volumes): SetEndOfFile plus SetFileValidData, so NTFS metadata ($MFT, directory indexes,
// bitmap) is real and at full scale while the VHDX on the host stays small. A share of the files is grown in steps
// while others grow too, so they fragment. For scan, catch-up, map and planning benchmarks; never for moves, which
// would write real data.
//
// The host file still grows by about one VHDX block per file: NTFS writes once where a new file's data begins (when
// the file turns non-resident), whatever the size, buffering or valid data length. Small files therefore cost their
// full size on the host, large ones about 1-2 MB each.
//
// Usage: make-scalevol <drive> --files N [--seed S] [--threads T] [--per-dir N] [--fragmented SHARE] [--min-size BYTES]
// [--max-size BYTES]
//   Writes below <drive>:\scale\. Only volumes labelled ICD-TEST*, never the Windows drive. Needs administrator rights
//   (SeManageVolumePrivilege for SetFileValidData).

#include "platform/win/Handle.h"
#include "platform/win/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

    using icd::platform::UniqueHandle;


    // Parsed command line.
    struct Options {
        std::wstring root; // L"Q:\\".
        std::uint64_t files = 0; // Files to create.
        std::uint64_t seed = 1; // Size and fragmentation choices.
        unsigned threads = 8; // Writers (each owns whole directories).
        std::uint64_t perDir = 1000; // Files per leaf directory.
        double fragmented = 0.1; // Share of files grown in interleaved steps.
        std::uint64_t minSize = 1024; // Smallest file.
        std::uint64_t maxSize = 16ULL << 20U; // Largest file.
        std::uint64_t cluster = 4096; // Cluster size of the volume (read from it).
    };

    // A number argument; throws on garbage.
    std::uint64_t number(const wchar_t* text) {
        wchar_t* end = nullptr;
        const auto value = std::wcstoull(text, &end, 10);
        if (end == text || *end != L'\0') {
            throw std::runtime_error("bad number");
        }
        return value;
    }

    // Applies one option with a value.
    void applyOption(Options& o, std::wstring_view name, const wchar_t* value) {
        if (name == L"--files") {
            o.files = number(value);
        }
        else if (name == L"--seed") {
            o.seed = number(value);
        }
        else if (name == L"--threads") {
            o.threads = static_cast<unsigned>(std::max<std::uint64_t>(1, number(value)));
        }
        else if (name == L"--per-dir") {
            o.perDir = std::max<std::uint64_t>(1, number(value));
        }
        else if (name == L"--fragmented") {
            o.fragmented = std::clamp(std::wcstod(value, nullptr), 0.0, 1.0);
        }
        else if (name == L"--min-size") {
            o.minSize = std::max<std::uint64_t>(1, number(value));
        }
        else if (name == L"--max-size") {
            o.maxSize = std::max<std::uint64_t>(4096, number(value));
        }
        else {
            throw std::runtime_error("unknown option");
        }
    }

    // Parses the command line; nullopt prints usage.
    std::optional<Options> parse(int argc, wchar_t** argv) {
        if (argc < 4 || std::iswalpha(argv[1][0]) == 0) {
            return std::nullopt;
        }
        Options o;
        o.root = std::wstring(1, static_cast<wchar_t>(std::towupper(argv[1][0]))) + L":\\";
        for (int i = 2; i + 1 < argc; i += 2) {
            applyOption(o, argv[i], argv[i + 1]);
        }
        o.maxSize = std::max(o.maxSize, o.minSize);
        return o.files == 0 ? std::nullopt : std::optional(o);
    }

    // Only ICD-TEST NTFS volumes, never the Windows drive: this tool creates millions of files.
    void checkTarget(const Options& o) {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        if (std::towupper(windows[0]) == o.root[0]) {
            throw std::runtime_error("refusing to write to the Windows drive");
        }
        wchar_t label[MAX_PATH + 1]{};
        wchar_t fs[MAX_PATH + 1]{};
        if (GetVolumeInformationW(o.root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH) == FALSE ||
            std::wstring(fs) != L"NTFS" || !std::wstring(label).starts_with(L"ICD-TEST")) {
            throw std::runtime_error("the volume is not an NTFS volume labelled ICD-TEST");
        }
    }

    // A file size: log-uniform from 1 KB to maxSize, so most files are small, as on real drives; rounded up to whole
    // clusters, because NTFS zero-fills the rest of a partly used last cluster, and those writes, one per file, would
    // grow the thin VHDX to nearly the allocated size.
    std::uint64_t pickSize(std::mt19937_64& random, const Options& o) {
        std::uniform_real_distribution<double> exponent(std::log2(static_cast<double>(o.minSize)),
                                                        std::log2(static_cast<double>(o.maxSize)));
        const auto size = static_cast<std::uint64_t>(std::exp2(exponent(random)));
        return (size + o.cluster - 1) / o.cluster * o.cluster;
    }

    // Cluster size of the volume.
    std::uint64_t clusterSize(const std::wstring& root) {
        DWORD sectorsPerCluster = 0;
        DWORD bytesPerSector = 0;
        DWORD freeClusters = 0;
        DWORD totalClusters = 0;
        if (GetDiskFreeSpaceW(root.c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters) ==
            FALSE) {
            throw std::runtime_error("cannot read the cluster size");
        }
        return std::uint64_t{sectorsPerCluster} * bytesPerSector;
    }

    // Opens a new file for writing.
    UniqueHandle create(const std::wstring& path) {
        UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            throw std::runtime_error("CreateFileW failed: " + std::to_string(GetLastError()));
        }
        return file;
    }

    // SetFileValidData calls that failed (resident files); reported so a missing privilege shows.
    std::atomic<std::uint64_t> validDataFailures{0};


    // Allocates clusters up to size without writing them.
    void allocate(HANDLE file, std::uint64_t size) {
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(size);
        if (SetFilePointerEx(file, end, nullptr, FILE_BEGIN) == FALSE || SetEndOfFile(file) == FALSE) {
            throw std::runtime_error("SetEndOfFile failed: " + std::to_string(GetLastError()));
        }
        if (SetFileValidData(file, end.QuadPart) == FALSE) { // Fails for files NTFS keeps resident.
            ++validDataFailures;
        }
    }

    // Shared progress of all writers.
    struct Progress {
        std::atomic<std::uint64_t> files{0}; // Files done.
        std::atomic<bool> failed{false}; // A writer stopped.
    };

    // Writes the files of one directory: the plain ones one by one, the fragmented ones four at a time, each grown in
    // eight steps in turn so their allocations interleave.
    void writeDirectory(const Options& o, std::uint64_t dir, std::uint64_t count, Progress& progress) {
        std::mt19937_64 random(o.seed * 1'000'003 + dir);
        const std::wstring path = o.root + L"scale\\a" + std::to_wstring(dir / 1000) + L"\\b" + std::to_wstring(dir);
        CreateDirectoryW((o.root + L"scale\\a" + std::to_wstring(dir / 1000)).c_str(), nullptr);
        CreateDirectoryW(path.c_str(), nullptr);
        std::bernoulli_distribution fragmented(o.fragmented);
        std::vector<std::pair<UniqueHandle, std::uint64_t>> group;
        const auto flush = [&]() {
            for (int step = 1; step <= 8; ++step) {
                for (auto& [file, size] : group) {
                    allocate(file.get(),
                             (size * static_cast<std::uint64_t>(step) / 8 + o.cluster - 1) / o.cluster * o.cluster);
                }
            }
            progress.files += group.size();
            group.clear();
        };
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t size = pickSize(random, o);
            UniqueHandle file = create(path + L"\\f" + std::to_wstring(i) + L".bin");
            if (fragmented(random)) {
                group.emplace_back(std::move(file), size);
                if (group.size() == 4) {
                    flush();
                }
                continue;
            }
            allocate(file.get(), size);
            ++progress.files;
        }
        flush();
    }

    // Runs the writers; each takes the next directory until all are done.
    void writeAll(const Options& o, Progress& progress) {
        const std::uint64_t dirs = (o.files + o.perDir - 1) / o.perDir;
        std::atomic<std::uint64_t> next{0};
        std::vector<std::jthread> writers;
        for (unsigned t = 0; t < o.threads; ++t) {
            writers.emplace_back([&]() {
                try {
                    for (std::uint64_t d = next++; d < dirs && !progress.failed; d = next++) {
                        writeDirectory(o, d, std::min(o.perDir, o.files - d * o.perDir), progress);
                    }
                }
                catch (const std::exception& e) {
                    std::fprintf(stderr, "\nmake-scalevol: %s\n", e.what());
                    progress.failed = true;
                }
            });
        }
        while (progress.files < o.files && !progress.failed) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::fprintf(stderr, "\r%llu of %llu files   ", static_cast<unsigned long long>(progress.files.load()),
                         static_cast<unsigned long long>(o.files));
        }
    }

} // namespace

// Entry point: checks, then writes and reports.
int wmain(int argc, wchar_t** argv) {
    try {
        auto options = parse(argc, argv);
        if (!options) {
            std::fprintf(stderr,
                         "Usage: make-scalevol <drive> --files N [--seed S] [--threads T] [--per-dir N] "
                         "[--fragmented SHARE] [--min-size BYTES] [--max-size BYTES]\n");
            return 2;
        }
        checkTarget(*options);
        options->cluster = clusterSize(options->root);
        if (!icd::platform::enablePrivilege(SE_MANAGE_VOLUME_NAME)) {
            throw std::runtime_error("SeManageVolumePrivilege is not available (run elevated)");
        }
        CreateDirectoryW((options->root + L"scale").c_str(), nullptr);
        const auto start = std::chrono::steady_clock::now();
        Progress progress;
        writeAll(*options, progress);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("\nmake-scalevol: %llu files on %ls in %.0f s (%llu SetFileValidData failures)\n",
                    static_cast<unsigned long long>(progress.files.load()), options->root.c_str(), seconds,
                    static_cast<unsigned long long>(validDataFailures.load()));
        return progress.failed ? 1 : 0;
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "make-scalevol: %s\n", e.what());
        return 1;
    }
}
