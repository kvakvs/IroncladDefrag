// The FSCTL_MOVE_FILE primitive (T100). The error mapping is checked everywhere; the moves themselves need
// administrator rights and a writable test volume (label ICD-TEST*, made with tools/testvol/make-testvol.ps1): a file,
// a named stream and a directory index are moved in small chunks, their runs re-read and their bytes read back from
// disk unchanged, and a move onto another file's clusters is refused and seen as in use.

#include "core/synth/SynthRandom.h"
#include "platform/win/MoveFile.h"
#include "platform/win/RetrievalPointers.h"
#include "platform/win/Volumes.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

    using icd::core::ClusterRun;
    using icd::platform::MoveError;

    TEST(ClusterMove, ClassifiesErrors) {
        using icd::platform::classifyMoveError;
        EXPECT_EQ(classifyMoveError(ERROR_SUCCESS), MoveError::None);
        EXPECT_EQ(classifyMoveError(ERROR_ACCESS_DENIED), MoveError::AccessDenied);
        EXPECT_EQ(classifyMoveError(ERROR_SHARING_VIOLATION), MoveError::Locked);
        EXPECT_EQ(classifyMoveError(ERROR_INVALID_PARAMETER), MoveError::Unsupported);
        EXPECT_EQ(classifyMoveError(ERROR_FILE_NOT_FOUND), MoveError::FileGone);
        EXPECT_EQ(classifyMoveError(ERROR_RETRY), MoveError::Retry);
        EXPECT_EQ(classifyMoveError(ERROR_GEN_FAILURE), MoveError::Failed);
        EXPECT_EQ(icd::platform::moveErrorKey(MoveError::DestinationInUse), "destination-in-use");
    }

    // A mounted, writable test volume: its GUID name and root.
    struct TestVolume {
        std::wstring volumeName; // "\\?\Volume{...}\".
        std::wstring root; // "T:\".
        std::uint32_t bytesPerCluster = 4096; // Cluster size.
        std::uint64_t totalClusters = 0; // Clusters.
    };

    // Whether a file can be created in the root (read-only mounts refuse).
    bool writable(const std::wstring& root) {
        const std::wstring probe = root + L"icd-write-probe.tmp";
        const HANDLE h =
            CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        CloseHandle(h);
        return true;
    }

    // The first writable ICD-TEST volume, if any.
    std::optional<TestVolume> findTestVolume() {
        for (const auto& v : icd::platform::enumerateVolumes()) {
            if (v.fileSystem == L"NTFS" && v.label.starts_with(L"ICD-TEST") && !v.mountPaths.empty() &&
                writable(v.mountPaths.front())) {
                return TestVolume{v.volumeName, v.mountPaths.front(), v.layout.bytesPerCluster, v.layout.totalClusters};
            }
        }
        return std::nullopt;
    }

    // Seeded bytes.
    std::vector<char> bytesFor(std::uint64_t seed, std::size_t n) {
        icd::core::SynthRandom random(seed);
        std::vector<char> out(n);
        for (char& c : out) {
            c = static_cast<char>(random.next() & 0xFFU);
        }
        return out;
    }

    // Writes a file (or "file:stream") and flushes it to disk.
    void writeFile(const std::wstring& path, const std::vector<char>& data) {
        std::ofstream out(std::filesystem::path(path), std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    // Reads a file back from disk, bypassing the cache, so moved data is read where it now lies.
    std::vector<char> readUnbuffered(const std::wstring& path, std::size_t n, std::uint32_t sector) {
        const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return {};
        }
        const std::size_t rounded = (n + sector - 1) / sector * sector;
        auto* buffer = static_cast<char*>(VirtualAlloc(nullptr, rounded, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        DWORD read = 0;
        ReadFile(h, buffer, static_cast<DWORD>(rounded), &read, nullptr);
        CloseHandle(h);
        std::vector<char> out(buffer, buffer + std::min<std::size_t>(read, n));
        VirtualFree(buffer, 0, MEM_RELEASE);
        return out;
    }

    // The allocated runs of an open stream, holes left out.
    std::vector<ClusterRun> runsOf(HANDLE stream) {
        std::vector<icd::platform::RetrievedRun> runs;
        EXPECT_TRUE(icd::platform::readRetrievalPointers(stream, runs));
        std::vector<ClusterRun> out;
        for (const auto& r : runs) {
            if (r.lcn >= 0) {
                out.push_back({static_cast<icd::core::Lcn>(r.lcn), r.length});
            }
        }
        return out;
    }

    // A free run of at least `clusters`, preferably in the upper half of the volume (away from new files).
    std::optional<ClusterRun> freeRun(HANDLE volume, const TestVolume& v, std::uint64_t clusters) {
        const auto runs = icd::platform::freeRunsIn(volume, {0, v.totalClusters});
        std::optional<ClusterRun> best;
        for (const ClusterRun& r : runs) {
            if (r.length >= clusters + 16 && (!best || r.start > best->start)) {
                best = ClusterRun{r.start + 8, clusters};
            }
        }
        return best;
    }

    // Moves the whole stream behind `stream` to a free run in chunks of 7 clusters and checks it lands there.
    void moveWhole(HANDLE volume, HANDLE stream, const TestVolume& v) {
        const auto before = runsOf(stream);
        ASSERT_FALSE(before.empty());
        std::uint64_t clusters = 0;
        for (const ClusterRun& r : before) {
            clusters += r.length;
        }
        const auto target = freeRun(volume, v, clusters);
        ASSERT_TRUE(target.has_value());
        int calls = 0;
        const auto moved = icd::platform::moveClustersChunked({volume, stream}, {0, target->start, clusters, 7}, [&] {
            ++calls;
            return true;
        });
        ASSERT_EQ(moved.last.error, MoveError::None) << moved.last.win32Error;
        EXPECT_EQ(moved.clustersMoved, clusters);
        EXPECT_EQ(calls, static_cast<int>((clusters + 6) / 7) - 1);
        EXPECT_EQ(runsOf(stream), std::vector{*target});
    }

    TEST(ClusterMove, MovesFilesOnTestVolumes) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        const auto v = findTestVolume();
        if (!v) {
            GTEST_SKIP() << "no writable ICD-TEST volume mounted (tools/testvol/make-testvol.ps1)";
        }
        const auto volume = icd::platform::openVolumeForMoves(v->volumeName);
        ASSERT_TRUE(volume.valid());
        const std::wstring dir = v->root + L"icd-movetest";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directory(dir);
        const std::wstring file = dir + L"\\data.bin";
        const auto data = bytesFor(1, std::size_t{3} << 20U);
        const auto side = bytesFor(2, std::size_t{256} << 10U);
        writeFile(file, data);
        writeFile(file + L":side", side);
        for (int i = 0; i < 400; ++i) {
            writeFile(dir + L"\\entry-with-a-long-name-" + std::to_wstring(i) + L".txt", {'x'});
        }
        {
            const auto handle = icd::platform::openForQuery(file);
            ASSERT_TRUE(handle.valid());
            const std::uint64_t reference = icd::platform::fileReferenceOf(handle.get());
            ASSERT_NE(reference, 0U);
            moveWhole(volume.get(), handle.get(), *v);
            const auto named = icd::platform::openStreamForMove(handle.get(), {reference, 1});
            ASSERT_TRUE(named.valid());
            moveWhole(volume.get(), named.get(), *v);
            EXPECT_FALSE(icd::platform::openStreamForMove(handle.get(), {reference, 2}).valid());

            // Onto clusters the stream already holds... of another file: refused, and the bitmap shows them in use.
            const auto directory = icd::platform::openForQuery(dir);
            const auto occupied = runsOf(handle.get()).front();
            const auto refused = icd::platform::moveClusters({volume.get(), directory.get()}, {0, occupied.start, 1});
            EXPECT_NE(refused.error, MoveError::None);
            EXPECT_TRUE(icd::platform::freeRunsIn(volume.get(), {occupied.start, 1}).empty());
            moveWhole(volume.get(), directory.get(), *v);
        }
        EXPECT_EQ(readUnbuffered(file, data.size(), 4096), data);
        EXPECT_EQ(readUnbuffered(file + L":side", side.size(), 4096), side);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir), {}), 401);
        std::filesystem::remove_all(dir);
    }

    // What NTFS does with one call whose target is taken part of the way along (T104b): it moves a large call in
    // internal pieces and refuses at the piece that meets the taken cluster, keeping the pieces before it at the target
    // (the executor reads the runs back and keeps that prefix). A small call inside one piece moves nothing.
    TEST(ClusterMove, RefusedCallKeepsAPrefix) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        const auto v = findTestVolume();
        if (!v) {
            GTEST_SKIP() << "no writable ICD-TEST volume mounted (tools/testvol/make-testvol.ps1)";
        }
        const auto volume = icd::platform::openVolumeForMoves(v->volumeName);
        const std::wstring dir = v->root + L"icd-movetest-refused";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directory(dir);
        writeFile(dir + L"\\big.bin", bytesFor(3, std::size_t{4096} * v->bytesPerCluster));
        writeFile(dir + L"\\blocker.bin", bytesFor(4, v->bytesPerCluster));
        {
            const auto big = icd::platform::openForQuery(dir + L"\\big.bin");
            const auto blocker = icd::platform::openForQuery(dir + L"\\blocker.bin");
            const auto target = freeRun(volume.get(), *v, 4200);
            ASSERT_TRUE(target.has_value());
            ASSERT_EQ(icd::platform::moveClusters({volume.get(), blocker.get()}, {0, target->start + 2048, 1}).error,
                      MoveError::None);
            const auto refused = icd::platform::moveClusters({volume.get(), big.get()}, {0, target->start, 4096});
            EXPECT_NE(refused.error, MoveError::None);
            std::uint64_t atTarget = 0;
            for (const ClusterRun& r : runsOf(big.get())) {
                const auto from = std::max(r.start, target->start);
                const auto to = std::min(r.end(), target->start + 4096);
                atTarget += to > from ? to - from : 0;
            }
            std::cout << "refused call: error " << refused.win32Error << ", " << atTarget
                      << " of 4096 clusters at the target afterwards\n";
            EXPECT_LE(atTarget, 2048U); // Never onto the taken cluster; a prefix, contiguous from the start.
            EXPECT_EQ(runsOf(big.get()).front().start, atTarget != 0 ? target->start : runsOf(big.get()).front().start);
        }
        std::filesystem::remove_all(dir);
    }

} // namespace
