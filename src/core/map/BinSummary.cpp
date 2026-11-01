#include "core/map/BinSummary.h"

#include <algorithm>
#include <cmath>

namespace icd::core {

    namespace {

        // A 0-255 value: part / whole scaled by scale, rounded.
        std::uint8_t ratio(double part, double whole, double scale) {
            return whole <= 0.0 ? 0 : static_cast<std::uint8_t>(std::lround(scale * part / whole));
        }

    } // namespace

    unsigned binUsedShift(int level) { return 2 * level > 23 ? static_cast<unsigned>(2 * level - 23) : 0U; }

    std::vector<std::uint32_t> recordBlocks(const Snapshot& snapshot, Executor& executor, const CancelToken& token) {
        const auto records = snapshot.extents.records();
        const std::uint64_t blockCount =
            (snapshot.geometry.totalClusters >> recordBlockShift) + 1; // Also covers a partial last block.
        std::vector<std::uint32_t> blocks(blockCount, static_cast<std::uint32_t>(records.size()));
        // Record i is the first ending after every block start in [end of record i - 1, end of record i), so each
        // record writes its own blocks and the chunks never overlap.
        executor.parallelFor(
            {.count = records.size(), .grain = 65536},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t index = begin; index < end; ++index) {
                    const Lcn from = index == 0 ? 0 : records[index - 1].end();
                    const std::uint64_t first = (from + (Lcn{1} << recordBlockShift) - 1) >> recordBlockShift;
                    const std::uint64_t last = std::min<std::uint64_t>(
                        blockCount, (records[index].end() + (Lcn{1} << recordBlockShift) - 1) >> recordBlockShift);
                    for (std::uint64_t block = first; block < last; ++block) {
                        blocks[block] = static_cast<std::uint32_t>(index);
                    }
                }
            },
            token);
        return blocks;
    }

    std::vector<std::uint8_t> fragmentedFileFlags(const Snapshot& snapshot, Executor& executor,
                                                  const CancelToken& token) {
        std::vector<std::uint8_t> out(snapshot.files.size(), 0);
        executor.parallelFor(
            {.count = out.size(), .grain = 65536},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t file = begin; file < end; ++file) {
                    out[file] = snapshot.extents.fragmentCount(static_cast<FileIndex>(file)) > 1 ? 1 : 0;
                }
            },
            token);
        return out;
    }

    namespace {

        // The map flags of one record.
        std::uint8_t mapFlagsOf(const ExtentRecord& record, const Snapshot& snapshot,
                                std::span<const std::uint8_t> fragmented) {
            const FileIndex file = record.file();
            const bool unmovable = (record.flags() & ExtentUnmovable) != 0 || snapshot.files.has(file, FileUnmovable);
            const bool directoryIndex = (record.flags() & ExtentDirectoryIndex) != 0;
            std::uint8_t flags = fragmented[file] != 0 ? ExtentOwnerFragmented : 0;
            flags |= unmovable ? ExtentOwnerUnmovable : 0;
            flags |= directoryIndex ? ExtentIsDirectoryIndex : 0;
            return flags;
        }

    } // namespace

    std::vector<std::uint8_t> extentMapFlags(const Snapshot& snapshot, Executor& executor, const CancelToken& token) {
        const std::vector<std::uint8_t> fragmented = fragmentedFileFlags(snapshot, executor, token);
        const auto records = snapshot.extents.records();
        std::vector<std::uint8_t> out(records.size(), 0);
        executor.parallelFor(
            {.count = out.size(), .grain = 65536},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t index = begin; index < end; ++index) {
                    out[index] = mapFlagsOf(records[index], snapshot, fragmented);
                }
            },
            token);
        return out;
    }

    namespace {

        // A per-file value, 0 when the column is shorter (or empty).
        std::uint8_t valueOf(std::span<const std::uint8_t> column, FileIndex file) {
            return file < column.size() ? column[file] : 0;
        }

        // Fills the owner's per-file facts (class, and heat and size unless they come per record).
        void addFileFacts(std::size_t record, const MapFileFacts& facts, PieceOwner& owner) {
            const bool perRecord = !facts.extentValues.empty();
            const bool perFile = !facts.kinds.empty() || (!perRecord && (!facts.heat.empty() || !facts.sizes.empty()));
            if (!perFile) {
                return; // Looked up only when present: the file columns are far apart in memory.
            }
            const FileIndex file = facts.snapshot.extents.records()[record].file();
            owner.kind = valueOf(facts.kinds, file);
            if (!perRecord) {
                owner.heat = valueOf(facts.heat, file);
                owner.size = valueOf(facts.sizes, file);
            }
        }

        // Fills the owner's heat and size from the per-record values, when there are any.
        void addRecordValues(std::size_t record, const MapFileFacts& facts, PieceOwner& owner) {
            if (record >= facts.extentValues.size()) {
                return;
            }
            const std::uint32_t packed = facts.extentValues[record];
            owner.heat = static_cast<std::uint8_t>((packed & 0xFU) * extentValueStep);
            owner.size = static_cast<std::uint8_t>((packed >> 4U) * extentValueStep);
        }

    } // namespace

    PieceOwner ownerOf(std::size_t record, const MapFileFacts& facts) {
        const std::uint8_t flags = record < facts.extentFlags.size() ? facts.extentFlags[record] : 0;
        PieceOwner owner;
        owner.fragmented = (flags & ExtentOwnerFragmented) != 0;
        owner.flags = static_cast<std::uint8_t>(((flags & ExtentOwnerUnmovable) != 0 ? BinUnmovable : 0) |
                                                ((flags & ExtentIsDirectoryIndex) != 0 ? BinDirectoryIndex : 0));
        addRecordValues(record, facts, owner);
        addFileFacts(record, facts, owner);
        return owner;
    }

    MapBin BinAccumulator::finish(int level) const {
        const auto used = static_cast<double>(used_);
        MapBin bin;
        bin.used = static_cast<std::uint32_t>(used_ >> binUsedShift(level));
        bin.kind = kind_;
        bin.fragmented = ratio(static_cast<double>(fragmented_), used, 255.0);
        bin.heat = ratio(static_cast<double>(heatSum_), used, 1.0);
        bin.size = ratio(static_cast<double>(sizeSum_), used, 1.0);
        bin.flags = flags_;
        return bin;
    }

} // namespace icd::core
