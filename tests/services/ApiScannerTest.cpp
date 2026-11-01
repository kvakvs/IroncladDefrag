// API scanner on the `special` test scenario (T047): every special case is flagged, the snapshot accounts for every
// cluster, and extents of sampled files match "fsutil file queryextents". Needs administrator rights and a test
// volume made with tools/testvol (gen-scenario special T); skipped otherwise.

#include "core/snapshot/ExtentIndex.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/ApiScanner.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <regex>
#include <unordered_map>

namespace {

    using icd::core::FileIndex;
    using icd::core::Snapshot;

    // A mounted test volume holding the special scenario, or nothing.
    std::optional<icd::platform::VolumeRecord> specialVolume() {
        for (auto& v : icd::platform::enumerateVolumes()) {
            const std::wstring root = v.mountPaths.front();
            if (v.label.starts_with(L"ICD-TEST") &&
                GetFileAttributesW((root + L"special").c_str()) != INVALID_FILE_ATTRIBUTES) {
                return v;
            }
        }
        return std::nullopt;
    }

    // One clean scan of a volume, shared by the tests (scanning takes a moment).
    struct Scanned {
        icd::platform::VolumeRecord volume; // What was scanned.
        icd::services::ApiScanResult result; // The scan.
        std::unordered_map<std::string, FileIndex> byPath; // "\special\a.bin" -> file.
        icd::core::ClusterCount bitmapFree = 0; // Free clusters per the bitmap.
    };

    const Scanned* scanned() {
        static const std::optional<Scanned> shared = []() -> std::optional<Scanned> {
            if (!icd::platform::isElevated()) {
                return std::nullopt;
            }
            auto volume = specialVolume();
            if (!volume) {
                return std::nullopt;
            }
            const auto handle = icd::platform::openVolume(volume->volumeName, GENERIC_READ);
            const icd::core::VolumeGeometry geometry{volume->layout.bytesPerCluster, volume->layout.totalClusters};
            icd::services::ThreadPool pool(4);
            const auto free = icd::platform::readFreeRuns(handle.get(), geometry.totalClusters, pool);
            Scanned s{*volume,
                      icd::services::scanFilesApi(handle.get(), volume->mountPaths.front(), geometry,
                                                  {free.runs().begin(), free.runs().end()}, {}, nullptr),
                      {},
                      free.freeClusters()};
            const Snapshot& snap = s.result.snapshot;
            for (FileIndex f = 0; f < snap.files.size(); ++f) {
                s.byPath.emplace(snap.path(f), f);
            }
            return s;
        }();
        return shared ? &*shared : nullptr;
    }

    // The file at a path; fails the test when missing.
    FileIndex fileAt(const Scanned& s, const std::string& path) {
        const auto it = s.byPath.find(path);
        EXPECT_NE(it, s.byPath.end()) << path;
        return it == s.byPath.end() ? icd::core::noFile : it->second;
    }

    // Whether a file has a flag.
    bool has(const Scanned& s, const std::string& path, icd::core::FileFlag flag) {
        const FileIndex f = fileAt(s, path);
        return f != icd::core::noFile && s.result.snapshot.files.has(f, flag);
    }

#define REQUIRE_SCAN()                                                                                                 \
    const Scanned* s = scanned();                                                                                      \
    if (s == nullptr) {                                                                                                \
        GTEST_SKIP() << "needs administrator rights and an ICD-TEST volume with the special scenario";                 \
    }

    TEST(ApiScanner, FlagsEverySpecialCase) {
        REQUIRE_SCAN();
        const Snapshot& snap = s->result.snapshot;
        using namespace icd::core;
        EXPECT_TRUE(has(*s, "\\special\\compressed\\text-0.log", FileCompressed));
        EXPECT_TRUE(has(*s, "\\special\\sparse.bin", FileSparse));
        EXPECT_TRUE(has(*s, "\\special\\efs\\secret.bin", FileEncrypted));
        EXPECT_TRUE(has(*s, "\\special\\links\\file-symlink.bin", FileReparsePoint));
        EXPECT_TRUE(has(*s, "\\special\\links\\dir-symlink", FileReparsePoint));
        EXPECT_TRUE(has(*s, "\\special\\links\\junction", FileReparsePoint));
        EXPECT_TRUE(has(*s, "\\special\\streams\\host.txt", FileAlternateStreams));
        EXPECT_TRUE(has(*s, "\\special\\resident\\r1.txt", FileResident));
        EXPECT_TRUE(has(*s, "\\special\\bigdir", FileDirectory));
        EXPECT_TRUE(has(*s, "\\$MFT", FileMetadata));
        EXPECT_TRUE(has(*s, "\\$MFT", FileUnmovable));
        EXPECT_FALSE(has(*s, "\\special\\odd\\readonly.bin", FileResident));
        // A hard-linked file appears once, under one of its three names.
        int linkNames = 0;
        for (const char* path :
             {"\\special\\links\\target.bin", "\\special\\links\\hard-1.bin", "\\special\\links\\other\\hard-2.bin"}) {
            const auto it = s->byPath.find(path);
            if (it != s->byPath.end()) {
                ++linkNames;
                EXPECT_TRUE(snap.files.has(it->second, FileHardLinked));
            }
        }
        EXPECT_EQ(linkNames, 1);
        // Unicode and long names survive.
        EXPECT_NE(fileAt(*s, "\\special\\odd\\\xD1\x84\xD0\xBE\xD1\x82\xD0\xBE-\xE6\x97\xA5\xE6\x9C\xAC.txt"), noFile);
        // Only NTFS system files refuse to open ($LogFile, $Bitmap, $Boot, ...); every user file is read.
        EXPECT_EQ(s->result.stats.unreadable, s->result.stats.unreadableFiles.size());
        for (const FileIndex f : s->result.stats.unreadableFiles) {
            EXPECT_TRUE(snap.files.has(f, FileMetadata)) << snap.path(f);
        }
    }

    TEST(ApiScanner, ExtentsCarryTheirKind) {
        REQUIRE_SCAN();
        const Snapshot& snap = s->result.snapshot;
        using namespace icd::core;
        const auto extentFlags = [&](const std::string& path) {
            std::uint16_t flags = 0;
            for (const std::uint32_t r : snap.extents.extentsOf(fileAt(*s, path))) {
                flags |= snap.extents.records()[r].flags();
            }
            return flags;
        };
        EXPECT_NE(extentFlags("\\special\\compressed\\text-2.log") & ExtentCompressionUnit, 0);
        EXPECT_NE(extentFlags("\\special\\bigdir") & ExtentDirectoryIndex, 0);
        EXPECT_NE(extentFlags("\\$MFT") & ExtentUnmovable, 0);
        EXPECT_TRUE(snap.extents.extentsOf(fileAt(*s, "\\special\\resident\\r1.txt")).empty());
        // The 3 MB alternate stream has clusters of its own (stream 1).
        bool streamExtent = false;
        for (const std::uint32_t r : snap.extents.extentsOf(fileAt(*s, "\\special\\streams\\host.txt"))) {
            streamExtent = streamExtent || snap.extents.records()[r].stream() == 1;
        }
        EXPECT_TRUE(streamExtent);
        // Sparse: only the 40 written 64 KB blocks are allocated.
        const FileIndex sparse = fileAt(*s, "\\special\\sparse.bin");
        EXPECT_LE(snap.files.allocatedClusters(sparse) * snap.geometry.bytesPerCluster, 40U * 64 * 1024);
        EXPECT_EQ(snap.files.sizeBytes(sparse), 1ULL << 30U);
        EXPECT_GE(snap.extents.fragmentCount(fileAt(*s, "\\special\\fragmented\\many.bin")), 1000U);
    }

    TEST(ApiScanner, AccountsForEveryCluster) {
        REQUIRE_SCAN();
        const Snapshot& snap = s->result.snapshot;
        icd::core::ClusterCount used = 0;
        for (const auto& record : snap.extents.records()) {
            used += record.length();
        }
        EXPECT_EQ(used + snap.freeSpace.freeClusters(), snap.geometry.totalClusters);
        // A quiet volume: the bitmap and the files agree, nothing changed during the scan.
        EXPECT_EQ(snap.freeSpace.freeClusters(), s->bitmapFree);
        EXPECT_EQ(s->result.stats.reconcile.droppedExtents, 0U);
        EXPECT_EQ(s->result.stats.reconcile.freeTaken, 0U);
        // Only unreadable system files remain unattributed: a few MB on a 4 GB volume, not file data.
        EXPECT_LT(s->result.stats.reconcile.unattributed * snap.geometry.bytesPerCluster, 64ULL << 20U);
    }

    // Runs of a file's main stream from "fsutil file queryextents", as (vcn, lcn, length), holes left out.
    std::vector<std::array<std::uint64_t, 3>> fsutilExtents(const std::string& path) {
        const std::string command = "fsutil file queryextents \"" + path + "\"";
        std::vector<std::array<std::uint64_t, 3>> runs;
        FILE* pipe = _popen(command.c_str(), "rt");
        if (pipe == nullptr) {
            return runs;
        }
        static const std::regex line(
            R"(VCN:\s*0x([0-9a-fA-F]+)\s+Clusters:\s*0x([0-9a-fA-F]+)\s+LCN:\s*0x([0-9a-fA-F]+))");
        char text[512];
        while (std::fgets(text, sizeof(text), pipe) != nullptr) {
            std::cmatch m;
            if (std::regex_search(text, m, line) && m[3].str().size() < 16) {
                runs.push_back(
                    {std::stoull(m[1], nullptr, 16), std::stoull(m[3], nullptr, 16), std::stoull(m[2], nullptr, 16)});
            }
        }
        _pclose(pipe);
        return runs;
    }

    // The snapshot's runs of a file's main stream, continuing runs joined, as (lcn, length).
    std::vector<std::array<std::uint64_t, 2>> snapshotRuns(const Snapshot& snap, FileIndex file) {
        std::vector<std::array<std::uint64_t, 2>> runs;
        for (const std::uint32_t r : snap.extents.extentsOf(file)) {
            const auto& record = snap.extents.records()[r];
            if (record.stream() != 0) {
                continue;
            }
            if (!runs.empty() && runs.back()[0] + runs.back()[1] == record.lcn()) {
                runs.back()[1] += record.length();
            }
            else {
                runs.push_back({record.lcn(), record.length()});
            }
        }
        return runs;
    }

    TEST(ApiScanner, ExtentsMatchFsutil) {
        REQUIRE_SCAN();
        const Snapshot& snap = s->result.snapshot;
        using namespace icd::core;
        std::vector<FileIndex> sample;
        for (FileIndex f = 0; f < snap.files.size(); ++f) {
            const auto flags = snap.files.flags(f);
            const bool plain =
                (flags &
                 (FileDirectory | FileMetadata | FileCompressed | FileSparse | FileReparsePoint | FileEncrypted)) == 0;
            // fsutil runs through an ANSI command line limited to 260-character paths.
            const std::string path = snap.path(f);
            const bool shellSafe = path.size() < 200 &&
                std::ranges::all_of(path, [](char c) { return static_cast<unsigned char>(c) < 128; });
            if (plain && shellSafe && !snap.extents.extentsOf(f).empty()) {
                sample.push_back(f);
            }
        }
        ASSERT_GE(sample.size(), 5U);
        const std::string root = icd::platform::toUtf8(s->volume.mountPaths.front());
        int compared = 0;
        for (std::size_t i = 0; i < sample.size(); i += std::max<std::size_t>(1, sample.size() / 25)) {
            const FileIndex f = sample[i];
            const std::string path = root + snap.path(f).substr(1);
            SCOPED_TRACE(path);
            std::vector<std::array<std::uint64_t, 2>> expected;
            for (const auto& [vcn, lcn, length] : fsutilExtents(path)) {
                if (!expected.empty() && expected.back()[0] + expected.back()[1] == lcn) {
                    expected.back()[1] += length;
                }
                else {
                    expected.push_back({lcn, length});
                }
            }
            EXPECT_EQ(snapshotRuns(snap, f), expected);
            ++compared;
        }
        EXPECT_GE(compared, 5);
    }

} // namespace
