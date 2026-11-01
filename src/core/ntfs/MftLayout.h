#pragma once

#include "core/ntfs/MftRecord.h"
#include "core/ntfs/RunList.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::ntfs {

    // Sizes that place records on the volume.
    struct MftGeometry {
        std::uint32_t bytesPerCluster = 4096; // Cluster size.
        std::uint32_t bytesPerRecord = 1024; // MFT record size (1 KB, or 4 KB on 4Kn disks).
    };

    // One read of the raw MFT scan: a cluster-aligned byte range of the volume holding whole records.
    struct MftChunk {
        std::uint64_t volumeOffset = 0; // Byte offset on the volume (cluster-aligned, so sector-aligned).
        std::uint32_t bytes = 0; // Bytes to read (whole clusters).
        std::uint32_t recordOffset = 0; // Where the first whole record starts inside the read.
        std::uint64_t firstRecord = 0; // Number of that record.
        std::uint32_t recordCount = 0; // Whole records inside the read.
    };

    // A piece of a record whose bytes straddle two $MFT runs (clusters smaller than records).
    struct RecordPiece {
        std::uint64_t volumeOffset = 0; // Byte offset on the volume.
        std::uint32_t bytes = 0; // Bytes of the record there.
    };

    // A record that no chunk holds whole, read piece by piece.
    struct SplitRecord {
        std::uint64_t record = 0; // Record number.
        std::vector<RecordPiece> pieces; // In record order.
    };

    // How to read $MFT: chunks in volume order (the head moves one way), records cut by run boundaries, and what
    // $MFT::$BITMAP let the scan skip.
    struct MftReadPlan {
        std::vector<MftChunk> chunks; // Sorted by volume offset.
        std::vector<SplitRecord> split; // Records read separately.
        std::uint64_t recordCount = 0; // Records $MFT holds.
        std::uint64_t skippedRecords = 0; // Unused records left out.
        std::uint64_t bytes = 0; // Bytes the chunks read.
    };

    // Limits of the plan.
    struct MftReadOptions {
        std::uint32_t maxChunkBytes = 16U << 20U; // One read (plan.md 6.2 principle 1: 8-16 MB).
        std::uint32_t minSkipBytes = 1U << 20U; // Unused stretches shorter than this are read anyway.
    };

    // Plans the raw MFT read from the runs of $MFT's $DATA (VCN order, holes not allowed) and its $BITMAP (bit n set:
    // record n in use; records past the bitmap count as used). Long unused stretches are skipped, chunks are capped and
    // cluster-aligned, and a record cut by a run boundary becomes a SplitRecord. Pure, so the plan is tested without
    // a volume.
    [[nodiscard]] MftReadPlan planMftReads(std::span<const DataRun> mftRuns, std::uint64_t recordCount,
                                           MftGeometry geometry, std::span<const std::byte> bitmap,
                                           MftReadOptions options = {});

    // Whether bit n of an in-use bitmap is set; bits past its end count as set.
    [[nodiscard]] bool isRecordUsed(std::span<const std::byte> bitmap, std::uint64_t record);

    // The runs of one non-resident attribute from its segments (any order; they may come from several records):
    // segments sorted by start VCN must continue each other from VCN 0. Returns nullopt when they leave gaps or
    // overlap, so a damaged $MFT is never read with a wrong map.
    [[nodiscard]] std::optional<std::vector<DataRun>> joinSegments(std::vector<const Attribute*> segments);

    // A byte range inside a stream.
    struct StreamBytes {
        std::uint64_t offset = 0; // First byte.
        std::uint64_t length = 0; // Bytes.
    };

    // Byte ranges on the volume that hold `range` of a stream with these runs (VCN order); empty when part of the
    // range is a hole or past the runs.
    [[nodiscard]] std::vector<RecordPiece> mapStreamBytes(std::span<const DataRun> runs, std::uint32_t bytesPerCluster,
                                                          StreamBytes range);

} // namespace icd::core::ntfs
