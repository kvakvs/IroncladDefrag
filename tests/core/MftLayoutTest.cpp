// Raw MFT read planning (T061): chunks cover every used record exactly once at the right volume offset, long unused
// stretches are skipped, chunks are capped and cluster-aligned, records cut by run boundaries are read in pieces.

#include "core/ntfs/MftLayout.h"

#include <gtest/gtest.h>

#include <map>
#include <random>

namespace icd::core::ntfs { namespace {

    // Volume offset of each record the plan reads (whole records in chunks, and split records' first piece).
    std::map<std::uint64_t, std::uint64_t> offsetsRead(const MftReadPlan& plan, MftGeometry g) {
        std::map<std::uint64_t, std::uint64_t> out;
        for (const MftChunk& c : plan.chunks) {
            EXPECT_EQ(c.volumeOffset % g.bytesPerCluster, 0U);
            EXPECT_EQ(c.bytes % g.bytesPerCluster, 0U);
            EXPECT_LE(c.recordOffset + std::uint64_t{c.recordCount} * g.bytesPerRecord, c.bytes);
            for (std::uint32_t i = 0; i < c.recordCount; ++i) {
                const bool fresh =
                    out.emplace(c.firstRecord + i, c.volumeOffset + c.recordOffset + i * g.bytesPerRecord).second;
                EXPECT_TRUE(fresh) << "record " << c.firstRecord + i << " read twice";
            }
        }
        for (const SplitRecord& s : plan.split) {
            EXPECT_TRUE(out.emplace(s.record, s.pieces.front().volumeOffset).second);
        }
        return out;
    }

    // A bitmap with the given records in use.
    std::vector<std::byte> bitmapOf(std::uint64_t records,
                                    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& used) {
        std::vector<std::byte> bits((records + 7) / 8);
        for (const auto& [first, end] : used) {
            for (std::uint64_t r = first; r < end; ++r) {
                bits[r / 8] |= static_cast<std::byte>(1U << (r % 8));
            }
        }
        return bits;
    }

    TEST(MftLayout, OneRunAllUsedIsCutIntoCappedChunks) {
        const MftGeometry g{4096, 1024};
        const std::vector<DataRun> runs{{0, 1000, 256}}; // 1 MB: 1024 records.
        const MftReadPlan plan = planMftReads(runs, 1024, g, {}, {.maxChunkBytes = 64 * 1024, .minSkipBytes = 1 << 20});
        const auto read = offsetsRead(plan, g);
        ASSERT_EQ(read.size(), 1024U);
        for (const auto& [record, offset] : read) {
            EXPECT_EQ(offset, 1000 * 4096 + record * 1024);
        }
        for (const MftChunk& c : plan.chunks) {
            EXPECT_LE(c.bytes, 64U * 1024);
        }
        EXPECT_EQ(plan.skippedRecords, 0U);
        EXPECT_TRUE(plan.split.empty());
    }

    TEST(MftLayout, SkipsLongUnusedStretchesOnly) {
        const MftGeometry g{4096, 1024};
        const std::vector<DataRun> runs{{0, 0, 4096}}; // 16 MB: 16384 records.
        // Used: 0-99, 120-199 (gap of 20: read), 5000-5100 (gap of 4800 records = 4.7 MB: skipped).
        const auto bitmap = bitmapOf(16384, {{0, 100}, {120, 200}, {5000, 5100}});
        const MftReadPlan plan =
            planMftReads(runs, 16384, g, bitmap, {.maxChunkBytes = 16 << 20, .minSkipBytes = 1 << 20});
        const auto read = offsetsRead(plan, g);
        ASSERT_EQ(plan.chunks.size(), 2U);
        EXPECT_TRUE(read.contains(110)); // Inside the short gap.
        EXPECT_FALSE(read.contains(1000));
        EXPECT_FALSE(read.contains(10000)); // Unused tail.
        for (std::uint64_t r : {0U, 99U, 120U, 199U, 5000U, 5099U}) {
            EXPECT_TRUE(read.contains(r)) << r;
        }
        EXPECT_EQ(plan.skippedRecords, 16384 - read.size());
    }

    TEST(MftLayout, FragmentedMftIsReadInVolumeOrder) {
        const MftGeometry g{4096, 1024};
        // VCN order goes backwards on disk.
        const std::vector<DataRun> runs{{0, 9000, 4}, {4, 5000, 4}, {8, 100, 4}};
        const MftReadPlan plan = planMftReads(runs, 48, g, {});
        ASSERT_EQ(plan.chunks.size(), 3U);
        EXPECT_LT(plan.chunks[0].volumeOffset, plan.chunks[1].volumeOffset);
        EXPECT_LT(plan.chunks[1].volumeOffset, plan.chunks[2].volumeOffset);
        const auto read = offsetsRead(plan, g);
        EXPECT_EQ(read.at(0), 9000U * 4096);
        EXPECT_EQ(read.at(16), 5000U * 4096);
        EXPECT_EQ(read.at(47), 100U * 4096 + 15 * 1024);
    }

    TEST(MftLayout, RecordsCutByRunBoundariesAreSplit) {
        const MftGeometry g{512, 1024}; // Two clusters per record.
        // Run boundaries at VCN 3 and 7 cut records 1 and 3.
        const std::vector<DataRun> runs{{0, 100, 3}, {3, 500, 4}, {7, 900, 9}};
        const MftReadPlan plan = planMftReads(runs, 8, g, {});
        ASSERT_EQ(plan.split.size(), 2U);
        EXPECT_EQ(plan.split[0].record, 1U);
        ASSERT_EQ(plan.split[0].pieces.size(), 2U);
        EXPECT_EQ(plan.split[0].pieces[0].volumeOffset, 102U * 512);
        EXPECT_EQ(plan.split[0].pieces[0].bytes, 512U);
        EXPECT_EQ(plan.split[0].pieces[1].volumeOffset, 500U * 512);
        EXPECT_EQ(plan.split[1].record, 3U);
        EXPECT_EQ(offsetsRead(plan, g).size(), 8U);
    }

    // Random layouts against a record-by-record mapping through mapStreamBytes().
    TEST(MftLayout, RandomLayoutsReadEveryUsedRecordOnce) {
        std::mt19937 random(7);
        for (int round = 0; round < 200; ++round) {
            const MftGeometry g{std::uint32_t{512} << (random() % 4), std::uint32_t{1024} << (random() % 2)};
            std::vector<DataRun> runs;
            std::uint64_t vcn = 0;
            for (int k = 0; k < 1 + static_cast<int>(random() % 6); ++k) {
                const std::uint64_t length = 1 + random() % 64;
                runs.push_back({vcn, static_cast<std::int64_t>(random() % 100000), length});
                vcn += length;
            }
            const std::uint64_t records = vcn * g.bytesPerCluster / g.bytesPerRecord;
            std::vector<std::byte> bitmap((records + 7) / 8);
            for (auto& b : bitmap) {
                b = static_cast<std::byte>(random() % 4 == 0 ? 0 : random());
            }
            const MftReadPlan plan =
                planMftReads(runs, records, g, bitmap, {.maxChunkBytes = 8192, .minSkipBytes = 2048});
            const auto read = offsetsRead(plan, g);
            for (std::uint64_t r = 0; r < records; ++r) {
                const auto pieces = mapStreamBytes(runs, g.bytesPerCluster, {r * g.bytesPerRecord, g.bytesPerRecord});
                if (isRecordUsed(bitmap, r)) {
                    ASSERT_TRUE(read.contains(r)) << "round " << round << " record " << r;
                    EXPECT_EQ(read.at(r), pieces.front().volumeOffset);
                }
            }
        }
    }

    TEST(MftLayout, JoinSegmentsNeedsContinuousVcns) {
        Attribute a;
        a.startVcn = 0;
        a.lastVcn = 9;
        a.runs = {{0, 100, 10}};
        Attribute b;
        b.startVcn = 10;
        b.lastVcn = 19;
        b.runs = {{10, 300, 10}};
        const auto joined = joinSegments({&b, &a});
        ASSERT_TRUE(joined);
        EXPECT_EQ(joined->size(), 2U);
        b.startVcn = 12;
        EXPECT_FALSE(joinSegments({&a, &b})); // Gap.
        b.startVcn = 5;
        EXPECT_FALSE(joinSegments({&a, &b})); // Overlap.
    }

    TEST(MftLayout, MapStreamBytesRefusesHoles) {
        const std::vector<DataRun> runs{{0, 10, 2}, {2, -1, 2}, {4, 50, 2}};
        EXPECT_EQ(mapStreamBytes(runs, 4096, {0, 8192}).size(), 1U);
        EXPECT_TRUE(mapStreamBytes(runs, 4096, {4096, 8192}).empty());
        EXPECT_TRUE(mapStreamBytes(runs, 4096, {20000, 8192}).empty()); // Past the end.
    }

}} // namespace icd::core::ntfs
