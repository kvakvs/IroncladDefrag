// MFT rows and merge (T061/T062) on handcrafted records: files in record order, names (DOS aliases skipped, hard
// links counted), parents with sequence check, metadata below $Extend, stream ordinals, flags, extents, and the same
// snapshot for any executor and any chunk order.

#include "core/NtfsRecordBuilder.h"

#include "core/ntfs/NtfsFormat.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftMerge.h"
#include "services/scan/MftRows.h"
#include "services/scan/ScanCompare.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace icd::services::mft { namespace {

    using core::ntfs::AttributeType;
    using core::ntfs::makeReference;
    using core::ntfs::NameSpace;
    using test::encodeRuns;
    using test::RecordBuilder;

    constexpr std::uint16_t dirFlags = core::ntfs::recordInUse | core::ntfs::recordDirectory;

    // References of the handcrafted volume's directories.
    const std::uint64_t rootRef = makeReference(5, 5);
    const std::uint64_t extendRef = makeReference(11, 11);
    const std::uint64_t photosRef = makeReference(16, 2);

    // Raw records of a small volume, by record number (1 KB records, 4 KB clusters, 10000 clusters).
    std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> volumeRecords() {
        std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> r;
        r.emplace_back(0,
                       RecordBuilder(0, 1)
                           .standard(1, 1, 1, 0x6)
                           .fileName(rootRef, u"$MFT")
                           .nonResident(AttributeType::Data, u"", 0, 7, encodeRuns({{100, 8}}), 32 * 1024)
                           .nonResident(AttributeType::Bitmap, u"", 0, 0, encodeRuns({{108, 1}}), 8)
                           .build());
        r.emplace_back(5,
                       RecordBuilder(5, 5, dirFlags)
                           .standard(2, 2, 2, 0x16)
                           .fileName(rootRef, u".")
                           .nonResident(AttributeType::IndexAllocation, u"$I30", 0, 0, encodeRuns({{200, 1}}), 4096)
                           .build());
        r.emplace_back(11,
                       RecordBuilder(11, 11, dirFlags).standard(3, 3, 3, 0x6).fileName(rootRef, u"$Extend").build());
        r.emplace_back(16, RecordBuilder(16, 2, dirFlags).standard(4, 4, 4, 0x10).fileName(rootRef, u"photos").build());
        // A fragmented file with a DOS alias, a named stream and a resident named stream.
        r.emplace_back(17,
                       RecordBuilder(17, 3)
                           .standard(10, 20, 30, 0x20)
                           .fileName(photosRef, u"Holiday picture.jpg", NameSpace::Win32)
                           .fileName(photosRef, u"HOLIDA~1.JPG", NameSpace::Dos)
                           .nonResident(AttributeType::Data, u"", 0, 9, encodeRuns({{1000, 4}, {3000, 6}}), 40000)
                           .nonResident(AttributeType::Data, u"thumb", 0, 1, encodeRuns({{4000, 2}}), 8000)
                           .resident(AttributeType::Data, u"tag", test::utf16(u"x"))
                           .build());
        // Hard links in two directories.
        r.emplace_back(18,
                       RecordBuilder(18, 1)
                           .standard(5, 5, 5, 0x20)
                           .fileName(photosRef, u"a.bin")
                           .fileName(rootRef, u"b.bin")
                           .nonResident(AttributeType::Data, u"", 0, 0, encodeRuns({{5000, 1}}), 100)
                           .build());
        // A resident file whose parent reference is stale (record 16 was reused): it goes to the root.
        r.emplace_back(19,
                       RecordBuilder(19, 1)
                           .standard(6, 6, 6, 0x20)
                           .fileName(makeReference(16, 1), u"orphan.txt")
                           .resident(AttributeType::Data, u"", test::utf16(u"hello"))
                           .build());
        // Metadata below $Extend with an extra (non-data) attribute, and a pinned root file.
        r.emplace_back(24,
                       RecordBuilder(24, 1)
                           .standard(7, 7, 7, 0x26)
                           .fileName(extendRef, u"$Reparse")
                           .nonResident(AttributeType::IndexAllocation, u"$R", 0, 0, encodeRuns({{6000, 1}}), 4096)
                           .build());
        r.emplace_back(25,
                       RecordBuilder(25, 1)
                           .standard(8, 8, 8, 0x26)
                           .fileName(rootRef, u"pagefile.sys")
                           .nonResident(AttributeType::Data, u"", 0, 99, encodeRuns({{7000, 100}}), 409600)
                           .build());
        // A sparse file and a record not in use.
        r.emplace_back(26,
                       RecordBuilder(26, 1)
                           .standard(9, 9, 9, 0x220)
                           .fileName(rootRef, u"sparse.bin")
                           .nonResident(AttributeType::Data, u"", 0, 99, encodeRuns({{-1, 90}, {8000, 10}}), 409600,
                                        core::ntfs::attributeSparse)
                           .build());
        r.emplace_back(27, RecordBuilder(27, 4, 0).fileName(rootRef, u"deleted.txt").build());
        return r;
    }

    // The records as chunks of `perChunk` consecutive record slots (gaps filled with empty records), shuffled.
    std::vector<std::shared_ptr<const ChunkRows>> chunksOf(std::size_t perChunk, unsigned seed) {
        const auto records = volumeRecords();
        const std::uint64_t count = records.back().first + 1;
        std::vector<std::byte> all(count * 1024);
        for (const auto& [n, raw] : records) {
            std::ranges::copy(raw, all.begin() + static_cast<std::ptrdiff_t>(n * 1024));
        }
        std::vector<std::shared_ptr<const ChunkRows>> chunks;
        for (std::uint64_t first = 0; first < count; first += perChunk) {
            auto rows = std::make_shared<ChunkRows>();
            const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(perChunk, count - first));
            parseChunk(std::span(all).subspan(first * 1024, std::size_t{n} * 1024), {first, n, 1024}, {}, *rows);
            chunks.push_back(std::move(rows));
        }
        std::ranges::shuffle(chunks, std::mt19937(seed));
        return chunks;
    }

    // Merges with an executor.
    MergeOutput merge(std::size_t perChunk, unsigned seed, core::Executor& executor) {
        const RowIndex rows(chunksOf(perChunk, seed));
        return mergeRows(rows, {}, {4096, 10000}, {{0, 100}, {9000, 1000}}, executor);
    }

    // The file of a path.
    core::FileIndex fileAt(const core::Snapshot& s, const std::string& path) {
        for (core::FileIndex f = 0; f < s.files.size(); ++f) {
            if (s.path(f) == path) {
                return f;
            }
        }
        ADD_FAILURE() << path;
        return 0;
    }

    TEST(MftMerge, BuildsFilesNamesAndParents) {
        core::InlineExecutor inline_;
        const MergeOutput out = merge(4, 1, inline_);
        const core::Snapshot& s = out.snapshot;
        // $MFT, root, $Extend, photos, 17, 18, 19, $Reparse, pagefile, sparse, placeholder (27 is not in use).
        ASSERT_EQ(s.files.size(), 11U);
        EXPECT_EQ(s.path(1), "\\");
        EXPECT_EQ(s.files.parent(1), core::noFile);
        const auto photo = fileAt(s, "\\photos\\Holiday picture.jpg"); // The Win32 name, not the alias.
        EXPECT_EQ(s.files.sizeBytes(photo), 40000U);
        EXPECT_EQ(s.files.times(photo).modified, 20);
        EXPECT_EQ(s.files.fileReference(photo), makeReference(17, 3));
        const auto links = fileAt(s, "\\photos\\a.bin"); // First name; both names count.
        EXPECT_TRUE(s.files.has(links, core::FileHardLinked));
        EXPECT_FALSE(s.files.has(photo, core::FileHardLinked)); // A DOS alias is no hard link.
        const auto orphan = fileAt(s, "\\orphan.txt"); // Stale parent: the root.
        EXPECT_TRUE(s.files.has(orphan, core::FileResident));
        EXPECT_EQ(s.files.sizeBytes(orphan), 10U);
        EXPECT_EQ(s.files.sizeBytes(1), 4096U); // A directory's size is its index allocation.
        EXPECT_EQ(out.stats.scanner, ScannerKind::Mft);
    }

    TEST(MftMerge, FlagsStreamsAndExtents) {
        core::InlineExecutor inline_;
        const core::Snapshot s = merge(4, 2, inline_).snapshot;
        const auto photo = fileAt(s, "\\photos\\Holiday picture.jpg");
        EXPECT_TRUE(s.files.has(photo, core::FileAlternateStreams));
        EXPECT_EQ(s.extents.fragmentCount(photo), 3U); // Two main-stream runs and the named stream's run.
        const auto& records = s.extents.records();
        const auto stream = [&](core::Lcn lcn) { return s.extents.find(lcn)->stream(); };
        EXPECT_EQ(stream(1000), 0);
        EXPECT_EQ(stream(4000), 1); // "thumb"; "tag" is resident and owns nothing.
        const auto mft = fileAt(s, "\\$MFT");
        EXPECT_TRUE(s.files.has(mft, core::FileMetadata));
        EXPECT_TRUE(s.files.has(mft, core::FileUnmovable));
        EXPECT_EQ(stream(108), extraStreamBase); // $MFT::$BITMAP: only the MFT path sees it.
        EXPECT_NE(s.extents.find(108)->flags() & core::ExtentUnmovable, 0);
        const auto reparse = fileAt(s, "\\$Extend\\$Reparse");
        EXPECT_TRUE(s.files.has(reparse, core::FileMetadata)); // Below $Extend.
        EXPECT_EQ(stream(6000), extraStreamBase); // $INDEX_ALLOCATION:$R is no directory index.
        const auto pagefile = fileAt(s, "\\pagefile.sys");
        EXPECT_TRUE(s.files.has(pagefile, core::FileUnmovable));
        EXPECT_FALSE(s.files.has(pagefile, core::FileMetadata));
        EXPECT_NE(s.extents.find(7000)->flags() & core::ExtentUnmovable, 0);
        const auto sparse = fileAt(s, "\\sparse.bin");
        EXPECT_TRUE(s.files.has(sparse, core::FileSparse));
        EXPECT_EQ(s.files.allocatedClusters(sparse), 10U); // Holes left out.
        EXPECT_NE(s.extents.find(200)->flags() & core::ExtentDirectoryIndex, 0);
        // Every cluster is free, in a file or with the placeholder.
        core::ClusterCount used = 0;
        for (const auto& r : records) {
            used += r.length();
        }
        EXPECT_EQ(used + s.freeSpace.freeClusters(), 10000U);
    }

    TEST(MftMerge, SameSnapshotForAnyExecutorAndChunking) {
        core::InlineExecutor inline_;
        const core::Snapshot reference = merge(28, 0, inline_).snapshot;
        ThreadPool pool(4);
        for (const std::size_t perChunk : {1U, 3U, 7U}) {
            for (unsigned seed = 0; seed < 3; ++seed) {
                const auto c = compareExact(reference, merge(perChunk, seed, pool).snapshot);
                EXPECT_TRUE(c.same()) << perChunk << "/" << seed << ": " << (c.samples.empty() ? "" : c.samples[0]);
            }
        }
    }

    TEST(MftRows, CountsDamagedRecordsAndSkipsUnused) {
        auto good = RecordBuilder(0).fileName(rootRef, u"x").build();
        auto bad = RecordBuilder(1).fileName(rootRef, u"y").build();
        bad[510] = std::byte{0x42}; // Torn.
        std::vector<std::byte> empty(1024);
        std::vector<std::byte> all;
        for (const auto* r : {&good, &bad, &empty}) {
            all.insert(all.end(), r->begin(), r->end());
        }
        ChunkRows rows;
        parseChunk(all, {0, 3, 1024}, {}, rows);
        EXPECT_EQ(rows.records.size(), 1U);
        EXPECT_EQ(rows.badRecords, 1U);
        EXPECT_EQ(rows.badRecordNumbers, std::vector<std::uint64_t>{1});
        ChunkRows skipped;
        const std::vector<std::byte> noneUsed{std::byte{0}};
        parseChunk(all, {0, 3, 1024}, noneUsed, skipped); // The bitmap says none is in use.
        EXPECT_TRUE(skipped.records.empty());
        EXPECT_EQ(skipped.badRecords, 0U);
    }

}} // namespace icd::services::mft
