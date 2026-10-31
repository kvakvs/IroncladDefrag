#include "core/map/BinSummary.h"

#include <cmath>

namespace icd::core {

    namespace {

        // A 0-255 value: part / whole scaled by scale, rounded.
        std::uint8_t ratio(double part, double whole, double scale) {
            return whole <= 0.0 ? 0 : static_cast<std::uint8_t>(std::lround(scale * part / whole));
        }

    } // namespace

    unsigned binUsedShift(int level) { return 2 * level > 31 ? static_cast<unsigned>(2 * level - 31) : 0U; }

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

    PieceOwner ownerOf(std::size_t record, const MapFileFacts& facts) {
        const std::uint8_t flags = record < facts.extentFlags.size() ? facts.extentFlags[record] : 0;
        PieceOwner owner;
        owner.fragmented = (flags & ExtentOwnerFragmented) != 0;
        owner.flags = static_cast<std::uint8_t>(((flags & ExtentOwnerUnmovable) != 0 ? BinUnmovable : 0) |
                                                ((flags & ExtentIsDirectoryIndex) != 0 ? BinDirectoryIndex : 0));
        // Kinds and heat are per file; looked up only when present.
        if (!facts.kinds.empty() || !facts.heat.empty()) {
            const FileIndex file = facts.snapshot.extents.records()[record].file();
            owner.kind = file < facts.kinds.size() ? facts.kinds[file] : 0;
            owner.heat = file < facts.heat.size() ? facts.heat[file] : 0;
        }
        return owner;
    }

    void BinAccumulator::add(ClusterCount clusters, const PieceOwner& owner) {
        used_ += clusters;
        fragmented_ += owner.fragmented ? clusters : 0;
        heatSum_ += clusters * owner.heat;
        flags_ |= owner.flags;
        if (clusters > largestPiece_) {
            largestPiece_ = clusters;
            kind_ = owner.kind;
        }
    }

    MapBin BinAccumulator::finish(int level) const {
        const auto used = static_cast<double>(used_);
        MapBin bin;
        bin.used = static_cast<std::uint32_t>(used_ >> binUsedShift(level));
        bin.kind = kind_;
        bin.fragmented = ratio(static_cast<double>(fragmented_), used, 255.0);
        bin.heat = ratio(static_cast<double>(heatSum_), used, 1.0);
        bin.flags = flags_;
        return bin;
    }

} // namespace icd::core
