// scan-bench: the plan.md 6.5 scan and catch-up targets on a real (scale) volume, the Phase 4 exit check (T067).
// On a mounted ICD-TEST volume with a USN journal it measures, in order:
//   1. bitmap read and free runs                       target: 15 s or less at 30 TB
//   2. full MFT scan, with the disk's busy share         target: bounded by the $MFT read, disk 90% or more busy
//   3. snapshot memory                                    target: 8 GB or less at 20 M files
//   4. snapshot save and load (compressed)
//   5. a "typical day" of changes, then the USN catch-up  target: 10 s or less; must equal a full rescan
//   6. the scan capped to 2 workers, a 2 GB budget and mapped storage, which must equal the uncapped one
// Step 5 writes to the volume (new files, growth, deletions, renames below \bench-changes), so the tool refuses
// volumes not labelled ICD-TEST. Needs administrator rights.
//
// Usage: scan-bench <drive> [--changes N] [--threads N] [--budget BYTES]

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/snapshot/SnapshotIO.h"
#include "platform/win/Compression.h"
#include "platform/win/Privileges.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftScanner.h"
#include "services/scan/ScanCompare.h"
#include "services/snapshot/MappedStorage.h"
#include "services/snapshot/UsnCatchUp.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

    using namespace icd;
    using Clock = std::chrono::steady_clock;

    // Command-line settings.
    struct Args {
        std::wstring root; // L"S:\\".
        std::size_t changes = 10'000; // Files created, grown, deleted and renamed in the "day" (a quarter each).
        std::size_t threads = 2; // Workers of the capped scan.
        std::uint64_t budget = std::uint64_t{2} << 30U; // Budget of the capped scan.
    };

    // Seconds since start.
    double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

    // Parses the command line; nullopt prints usage.
    std::optional<Args> parse(int argc, wchar_t** argv) {
        if (argc < 2 || std::iswalpha(argv[1][0]) == 0) {
            return std::nullopt;
        }
        Args a;
        a.root = std::wstring(1, static_cast<wchar_t>(std::towupper(argv[1][0]))) + L":\\";
        for (int i = 2; i + 1 < argc; i += 2) {
            const std::wstring_view name = argv[i];
            const std::uint64_t value = std::wcstoull(argv[i + 1], nullptr, 10);
            a.changes = name == L"--changes" ? value : a.changes;
            a.threads = name == L"--threads" ? value : a.threads;
            a.budget = name == L"--budget" ? value : a.budget;
        }
        return a;
    }

    // The mounted volume at root; throws unless it is an ICD-TEST NTFS volume.
    platform::VolumeRecord findVolume(const std::wstring& root) {
        for (auto& v : platform::enumerateVolumes()) {
            if (!v.mountPaths.empty() && _wcsicmp(v.mountPaths.front().c_str(), root.c_str()) == 0) {
                if (!v.label.starts_with(L"ICD-TEST") || !v.layout.ntfs) {
                    throw std::runtime_error("scan-bench writes changes: only NTFS volumes labelled ICD-TEST");
                }
                return v;
            }
        }
        throw std::runtime_error("no volume mounted there");
    }

    // One result line with its target.
    void line(const char* what, const std::string& value, const char* target) {
        std::printf("  %-34s %-40s %s\n", what, value.c_str(), target);
    }

    // Creates a file of `size` allocated bytes (not written).
    void createFile(const std::filesystem::path& path, std::uint64_t size) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("cannot create " + path.string());
        }
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(size);
        SetFilePointerEx(file, end, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        SetFileValidData(file, end.QuadPart);
        CloseHandle(file);
    }

    // A "typical day": a quarter each of new files, files grown, deleted and renamed, spread over the volume.
    void makeChanges(const Args& a) {
        platform::enablePrivilege(SE_MANAGE_VOLUME_NAME);
        const std::filesystem::path dir = std::filesystem::path(a.root) / L"bench-changes";
        std::filesystem::create_directories(dir);
        const std::size_t quarter = a.changes / 4;
        std::mt19937_64 random(a.changes);
        for (std::size_t i = 0; i < quarter; ++i) {
            createFile(dir / (L"new-" + std::to_wstring(i) + L".bin"), 4096 * (1 + random() % 64));
            createFile(dir / (L"grow-" + std::to_wstring(i) + L".bin"), 4096);
            createFile(dir / (L"grow-" + std::to_wstring(i) + L".bin"), 4096 * (2 + random() % 64));
        }
        for (std::size_t i = 0; i < quarter; ++i) {
            std::filesystem::remove(dir / (L"new-" + std::to_wstring(i) + L".bin"));
            createFile(dir / (L"keep-" + std::to_wstring(i) + L".bin"), 4096 * (1 + random() % 16));
            std::filesystem::rename(dir / (L"keep-" + std::to_wstring(i) + L".bin"),
                                    dir / (L"renamed-" + std::to_wstring(i) + L".bin"));
        }
        // Flush so the raw MFT reads see every change (the bench owns this test volume).
        HANDLE volume = CreateFileW((L"\\\\.\\" + a.root.substr(0, 2)).c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (volume != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(volume);
            CloseHandle(volume);
        }
    }

    // A full MFT scan (bitmap included) with an executor and optional storage.
    core::Snapshot fullScan(HANDLE volume, const platform::VolumeRecord& v, core::Executor& executor,
                            services::MftScanOptions options, services::ScanStats* stats = nullptr) {
        const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
        const auto free = platform::readFreeRuns(volume, geometry.totalClusters, executor);
        auto result = services::scanFilesMft(volume, v.volumeName, geometry, *v.layout.ntfs,
                                             {free.runs().begin(), free.runs().end()}, executor, {}, nullptr, options);
        if (stats != nullptr) {
            *stats = result.stats;
        }
        return std::move(result.snapshot);
    }

    // Steps 1-4.
    core::Snapshot scanAndStore(HANDLE volume, const platform::VolumeRecord& v, core::Executor& pool) {
        const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
        auto start = Clock::now();
        const auto free = platform::readFreeRuns(volume, geometry.totalClusters, pool);
        line("bitmap and free runs", std::format("{:.2f} s, {} runs", since(start), core::formatCount(free.size())),
             "<= 15 s at 30 TB");
        start = Clock::now();
        auto scan = services::scanFilesMft(volume, v.volumeName, geometry, *v.layout.ntfs,
                                           {free.runs().begin(), free.runs().end()}, pool, {}, nullptr);
        const double seconds = since(start);
        const services::MftReadFigures& f = *scan.stats.mft;
        line("full MFT scan",
             std::format("{:.2f} s, {} files, {} extents", seconds, core::formatCount(scan.snapshot.files.size()),
                         core::formatCount(scan.snapshot.extents.size())),
             "bounded by the $MFT read");
        line("  $MFT read",
             std::format("{} in {:.2f} s ({:.0f} MB/s)", core::formatBytes(f.bytesRead), f.readSeconds,
                         static_cast<double>(f.bytesRead) / 1e6 / std::max(f.readSeconds, 1e-9)),
             "");
        line("  disk busy during the read",
             std::format("{:.1f}% (device counters {:.1f}%)", f.readerBusyPercent, f.deviceBusyPercent), ">= 90%");
        line("snapshot memory", core::formatBytes(core::snapshotMemoryBytes(scan.snapshot)), "<= 8 GB at 20 M files");
        core::MemorySink sink;
        const platform::XpressCodec codec;
        start = Clock::now();
        core::writeSnapshotFile(scan.snapshot, {}, sink, codec, pool);
        const double save = since(start);
        start = Clock::now();
        core::MemorySource source(sink.bytes);
        (void)core::readSnapshotFile(source, codec, pool);
        line(
            "snapshot save / load",
            std::format("{:.2f} s / {:.2f} s, {} compressed", save, since(start), core::formatBytes(sink.bytes.size())),
            "");
        return std::move(scan.snapshot);
    }

    // Steps 5 and 6; returns whether both comparisons held.
    bool catchUpAndCaps(HANDLE volume, const platform::VolumeRecord& v, core::Snapshot saved, core::Executor& pool,
                        const Args& a) {
        auto start = Clock::now();
        makeChanges(a);
        line("typical day of changes",
             std::format("{} changes in {:.1f} s", core::formatCount(a.changes), since(start)), "");
        const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
        start = Clock::now();
        const auto caught =
            services::catchUpSnapshot(volume, v.volumeName, std::move(saved), *v.layout.ntfs, geometry, pool, {});
        line("USN catch-up",
             std::format("{:.2f} s, {} journal records, {} records read, {}", since(start),
                         core::formatCount(caught.journalRecords), core::formatCount(caught.recordsRead),
                         caught.patched ? "patched" : "full finish"),
             "<= 10 s");
        const core::Snapshot rescan = fullScan(volume, v, pool, {});
        const bool caughtUp = caught.outcome != services::CatchUpOutcome::RescanNeeded &&
            services::compareExact(rescan, caught.snapshot, {.accessTimes = false}).same();
        line("  catch-up = full rescan", caughtUp ? "yes" : ("NO " + caught.reason), "must hold");
        services::ThreadPool capped(a.threads);
        core::MemoryPlan plan = core::planMemory(core::estimateSnapshot(rescan.files.size()), a.budget);
        while (!plan.mappedStorage()) {
            plan = core::giveWay(plan, core::estimateSnapshot(rescan.files.size()));
        }
        services::MftScanOptions options;
        options.storage = services::storageFor(plan);
        start = Clock::now();
        const core::Snapshot small = fullScan(volume, v, capped, options);
        const bool same = services::compareExact(rescan, small).same();
        line("capped scan (mapped storage)",
             std::format("{} workers, {:.2f} s, {}", a.threads, since(start), same ? "identical" : "DIFFERENT"),
             "must be identical");
        return caughtUp && same;
    }

} // namespace

// Entry point.
int wmain(int argc, wchar_t** argv) {
    try {
        const auto args = parse(argc, argv);
        if (!args) {
            std::fprintf(stderr, "Usage: scan-bench <drive> [--changes N] [--threads N] [--budget BYTES]\n");
            return 2;
        }
        const platform::VolumeRecord v = findVolume(args->root);
        const auto volume = platform::openVolume(v.volumeName, GENERIC_READ);
        services::ThreadPool pool(std::max<std::size_t>(1, std::thread::hardware_concurrency() - 1));
        std::printf("scan-bench %ls: %s, %u-byte clusters, %zu workers\n", args->root.c_str(),
                    core::formatBytes(v.totalBytes).c_str(), v.layout.bytesPerCluster, pool.concurrency());
        core::Snapshot snapshot = scanAndStore(volume.get(), v, pool);
        if (!snapshot.usn) {
            throw std::runtime_error("the volume has no USN journal (fsutil usn createjournal m=... a=... <drive>:)");
        }
        const bool ok = catchUpAndCaps(volume.get(), v, std::move(snapshot), pool, *args);
        std::printf("%s\n", ok ? "All checks hold." : "A check FAILED.");
        return ok ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "scan-bench: %s\n", e.what());
        return 1;
    }
}
