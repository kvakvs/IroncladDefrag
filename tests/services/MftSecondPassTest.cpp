// Second pass over files spread over several MFT records (T062): extension records join their base file only when the
// base's attribute list names them with the right sequence number, non-resident lists are read through the reader, an
// unreadable list leaves the file with its base record, and the merged file has every name and run in VCN order.

#include "core/NtfsRecordBuilder.h"

#include "core/ntfs/NtfsFormat.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftMerge.h"
#include "services/scan/MftRows.h"
#include "services/scan/MftSecondPass.h"
#include "services/scan/ScanCompare.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace icd::services::mft { namespace {

    using core::ntfs::AttributeType;
    using core::ntfs::makeReference;
    using test::encodeRuns;
    using test::listEntry;
    using test::RecordBuilder;

    const std::uint64_t rootRef = makeReference(5, 5);
    const std::uint64_t bigRef = makeReference(30, 2);
    const std::uint64_t farRef = makeReference(40, 1);
    const std::uint64_t badRef = makeReference(50, 1);

    // Attribute list of the non-resident case (record 40), as stored in clusters 500.
    std::vector<std::byte> farList() {
        return test::joinBytes({listEntry(AttributeType::FileName, 0, farRef, 1, u""),
                                listEntry(AttributeType::Data, 0, makeReference(41, 1), 0, u"")});
    }

    // Records of the volume (1000 clusters of 4 KB).
    std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> records() {
        std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> r;
        r.emplace_back(
            5,
            RecordBuilder(5, 5, core::ntfs::recordInUse | core::ntfs::recordDirectory).fileName(rootRef, u".").build());
        // A file whose $DATA lives in two extension records, with a second name in one of them.
        const auto bigList = test::joinBytes({listEntry(AttributeType::StandardInformation, 0, bigRef, 0, u""),
                                              listEntry(AttributeType::FileName, 0, bigRef, 1, u""),
                                              listEntry(AttributeType::FileName, 0, makeReference(32, 1), 1, u""),
                                              listEntry(AttributeType::Data, 0, makeReference(31, 1), 0, u""),
                                              listEntry(AttributeType::Data, 10, makeReference(32, 1), 0, u""),
                                              listEntry(AttributeType::Data, 20, makeReference(34, 4), 0, u"")});
        r.emplace_back(30,
                       RecordBuilder(30, 2)
                           .standard(1, 2, 3, 0x20)
                           .fileName(rootRef, u"big.bin")
                           .resident(AttributeType::AttributeList, u"", bigList)
                           .build());
        r.emplace_back(31,
                       RecordBuilder(31, 1)
                           .base(bigRef)
                           .nonResident(AttributeType::Data, u"", 0, 9, encodeRuns({{100, 10}}), 80000)
                           .build());
        r.emplace_back(32,
                       RecordBuilder(32, 1)
                           .base(bigRef)
                           .nonResident(AttributeType::Data, u"", 10, 19, encodeRuns({{300, 10}}), 0)
                           .fileName(rootRef, u"link.bin")
                           .build());
        // Unlisted, and listed with another sequence number (the record was reused): both rejected.
        r.emplace_back(33,
                       RecordBuilder(33, 1)
                           .base(bigRef)
                           .nonResident(AttributeType::Data, u"", 20, 22, encodeRuns({{700, 3}}), 0)
                           .build());
        r.emplace_back(34,
                       RecordBuilder(34, 5)
                           .base(bigRef)
                           .nonResident(AttributeType::Data, u"", 20, 21, encodeRuns({{800, 2}}), 0)
                           .build());
        // A file whose list is non-resident.
        r.emplace_back(
            40,
            RecordBuilder(40, 1)
                .standard(1, 1, 1, 0x20)
                .fileName(rootRef, u"far.bin")
                .nonResident(AttributeType::AttributeList, u"", 0, 0, encodeRuns({{500, 1}}), farList().size())
                .build());
        r.emplace_back(41,
                       RecordBuilder(41, 1)
                           .base(farRef)
                           .nonResident(AttributeType::Data, u"", 0, 4, encodeRuns({{600, 5}}), 20000)
                           .build());
        // A file whose list cannot be read.
        r.emplace_back(50,
                       RecordBuilder(50, 1)
                           .standard(1, 1, 1, 0x20)
                           .fileName(rootRef, u"bad.bin")
                           .nonResident(AttributeType::AttributeList, u"", 0, 0, encodeRuns({{900, 1}}), 64)
                           .build());
        r.emplace_back(51,
                       RecordBuilder(51, 1)
                           .base(badRef)
                           .nonResident(AttributeType::Data, u"", 0, 0, encodeRuns({{950, 1}}), 4096)
                           .build());
        return r;
    }

    // One chunk per record.
    std::vector<std::shared_ptr<const ChunkRows>> chunks() {
        std::vector<std::shared_ptr<const ChunkRows>> out;
        for (auto& [number, raw] : records()) {
            auto rows = std::make_shared<ChunkRows>();
            parseChunk(raw, {number, 1, 1024}, {}, *rows);
            out.push_back(std::move(rows));
        }
        return out;
    }

    // Serves the list in cluster 500 and fails for anything else.
    std::vector<std::byte> readList(std::span<const core::ntfs::DataRun> runs, std::uint64_t size) {
        if (runs.size() == 1 && runs[0].lcn == 500 && size == farList().size()) {
            return farList();
        }
        throw std::runtime_error("unreadable");
    }

    // Second pass and merge with an executor.
    std::pair<core::Snapshot, SecondPassStats> scan(core::Executor& executor) {
        const RowIndex rows(chunks());
        SecondPassStats stats;
        const ExtensionMap extensions = joinExtensionRecords(rows, readList, executor, stats);
        return {mergeRows(rows, extensions, {4096, 1000}, {{0, 100}}, executor).snapshot, stats};
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

    TEST(MftSecondPass, JoinsOnlyListedCurrentExtensionRecords) {
        core::InlineExecutor serial;
        const auto [s, stats] = scan(serial);
        EXPECT_EQ(stats.listFiles, 3U);
        EXPECT_EQ(stats.accepted, 3U); // 31, 32, 41.
        EXPECT_EQ(stats.rejected, 3U); // 33 unlisted, 34 stale, 51 of the file with the unreadable list.
        EXPECT_EQ(stats.nonResidentLists, 2U);
        EXPECT_EQ(stats.badLists, 1U);

        const auto big = fileAt(s, "\\big.bin");
        EXPECT_EQ(s.files.allocatedClusters(big), 20U); // Not the 5 clusters of the rejected records.
        EXPECT_EQ(s.files.sizeBytes(big), 80000U); // From the first segment, in an extension record.
        EXPECT_TRUE(s.files.has(big, core::FileHardLinked)); // The second name is in record 32.
        const auto parts = s.extents.extentsOf(big);
        ASSERT_EQ(parts.size(), 2U);
        EXPECT_EQ(s.extents.records()[parts[0]].lcn(), 100U); // VCN order.
        EXPECT_EQ(s.extents.records()[parts[1]].lcn(), 300U);
        EXPECT_EQ(s.extents.find(700)->file(), s.files.size() - 1); // Rejected: the placeholder holds them.
        EXPECT_EQ(s.extents.find(800)->file(), s.files.size() - 1);

        const auto far = fileAt(s, "\\far.bin");
        EXPECT_EQ(s.files.allocatedClusters(far), 6U); // Five of data and the list's own cluster.
        EXPECT_EQ(s.files.sizeBytes(far), 20000U);
        EXPECT_EQ(s.extents.find(500)->file(), far); // The list's own cluster, as an extra stream.
        EXPECT_GE(s.extents.find(500)->stream(), extraStreamBase);

        const auto bad = fileAt(s, "\\bad.bin");
        EXPECT_EQ(s.files.allocatedClusters(bad), 1U); // Only the list's cluster; its data stays unattributed.
        EXPECT_FALSE(s.files.has(bad, core::FileResident)); // Its data was not seen, so it is not known resident.
    }

    TEST(MftSecondPass, SameResultForAnyExecutor) {
        core::InlineExecutor serial;
        ThreadPool pool(4);
        const auto c = compareExact(scan(serial).first, scan(pool).first);
        EXPECT_TRUE(c.same()) << (c.samples.empty() ? "" : c.samples.front());
    }

}} // namespace icd::services::mft
