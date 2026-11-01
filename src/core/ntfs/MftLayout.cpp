#include "core/ntfs/MftLayout.h"

#include <algorithm>

namespace icd::core::ntfs {

    namespace {

        // One $MFT run seen as stream bytes and volume bytes.
        struct RunBytes {
            std::uint64_t streamStart = 0; // Byte offset in $MFT.
            std::uint64_t streamEnd = 0; // One past the last byte.
            std::uint64_t volumeStart = 0; // Byte offset on the volume of streamStart.
        };

        // The first record at or after `from` that is in use, or `end`.
        std::uint64_t nextUsed(std::span<const std::byte> bitmap, std::uint64_t from, std::uint64_t end) {
            std::uint64_t r = from;
            while (r < end && !isRecordUsed(bitmap, r)) {
                const bool wholeByte = r % 8 == 0 && r / 8 < bitmap.size() && bitmap[r / 8] == std::byte{0};
                r += wholeByte ? 8 : 1; // Skip empty bytes at once.
            }
            return std::min(r, end);
        }

        // Builds the chunks of one run.
        class RunPlanner {
        public:
            RunPlanner(MftReadPlan& plan, MftGeometry geometry, std::span<const std::byte> bitmap,
                       MftReadOptions options) :
                plan_(plan), geometry_(geometry), bitmap_(bitmap),
                maxRecords_(std::max<std::uint64_t>(
                    1,
                    (options.maxChunkBytes - std::min(options.maxChunkBytes, 2 * geometry.bytesPerCluster)) /
                        geometry.bytesPerRecord)),
                skipRecords_(std::max<std::uint64_t>(1, options.minSkipBytes / geometry.bytesPerRecord)) {}

            // Chunks for the whole records [first, end) of a run.
            void plan(const RunBytes& run, std::uint64_t first, std::uint64_t end) {
                std::uint64_t r = nextUsed(bitmap_, first, end);
                while (r < end) {
                    const std::uint64_t last = lastOfChunk(r, end);
                    emit(run, r, last + 1);
                    r = nextUsed(bitmap_, last + 1, end);
                }
            }

        private:
            // The last used record of a chunk starting at a used record: it ends at the size cap, the run's end or
            // the start of a long unused stretch.
            std::uint64_t lastOfChunk(std::uint64_t start, std::uint64_t end) const {
                std::uint64_t last = start;
                std::uint64_t gap = 0;
                for (std::uint64_t r = start + 1; r < end && r - start < maxRecords_ && gap < skipRecords_; ++r) {
                    const bool used = isRecordUsed(bitmap_, r);
                    last = used ? r : last;
                    gap = used ? 0 : gap + 1;
                }
                return last;
            }

            // Adds the cluster-aligned read of records [first, end).
            void emit(const RunBytes& run, std::uint64_t first, std::uint64_t end) {
                const std::uint64_t cluster = geometry_.bytesPerCluster;
                const std::uint64_t begin = run.volumeStart + (first * geometry_.bytesPerRecord - run.streamStart);
                const std::uint64_t finish = run.volumeStart + (end * geometry_.bytesPerRecord - run.streamStart);
                const std::uint64_t alignedBegin = begin / cluster * cluster;
                const std::uint64_t alignedEnd = (finish + cluster - 1) / cluster * cluster;
                plan_.chunks.push_back({.volumeOffset = alignedBegin,
                                        .bytes = static_cast<std::uint32_t>(alignedEnd - alignedBegin),
                                        .recordOffset = static_cast<std::uint32_t>(begin - alignedBegin),
                                        .firstRecord = first,
                                        .recordCount = static_cast<std::uint32_t>(end - first)});
                plan_.bytes += alignedEnd - alignedBegin;
            }

            MftReadPlan& plan_; // Being filled.
            MftGeometry geometry_; // Cluster and record size.
            std::span<const std::byte> bitmap_; // Records in use.
            std::uint64_t maxRecords_; // Records per chunk at most.
            std::uint64_t skipRecords_; // Unused records that end a chunk.
        };

        // Adds the record cut by the end of a run, if it is in use.
        void addSplit(MftReadPlan& plan, std::span<const DataRun> runs, MftGeometry geometry,
                      std::span<const std::byte> bitmap, std::uint64_t record) {
            const bool seen = !plan.split.empty() && plan.split.back().record == record; // Cut by two run ends.
            if (record >= plan.recordCount || seen || !isRecordUsed(bitmap, record)) {
                return;
            }
            auto pieces = mapStreamBytes(runs, geometry.bytesPerCluster,
                                         {record * geometry.bytesPerRecord, geometry.bytesPerRecord});
            if (!pieces.empty()) {
                plan.split.push_back({record, std::move(pieces)});
            }
        }

        // Records read by the plan.
        std::uint64_t recordsRead(const MftReadPlan& plan) {
            std::uint64_t read = plan.split.size();
            for (const MftChunk& chunk : plan.chunks) {
                read += chunk.recordCount;
            }
            return read;
        }

    } // namespace

    bool isRecordUsed(std::span<const std::byte> bitmap, std::uint64_t record) {
        if (record / 8 >= bitmap.size()) {
            return true;
        }
        return (static_cast<std::uint8_t>(bitmap[record / 8]) & (1U << (record % 8))) != 0;
    }

    MftReadPlan planMftReads(std::span<const DataRun> mftRuns, std::uint64_t recordCount, MftGeometry geometry,
                             std::span<const std::byte> bitmap, MftReadOptions options) {
        MftReadPlan plan;
        plan.recordCount = recordCount;
        RunPlanner planner(plan, geometry, bitmap, options);
        const std::uint64_t rs = geometry.bytesPerRecord;
        for (const DataRun& run : mftRuns) {
            if (run.hole()) {
                continue;
            }
            const RunBytes bytes{.streamStart = run.vcn * geometry.bytesPerCluster,
                                 .streamEnd = (run.vcn + run.length) * geometry.bytesPerCluster,
                                 .volumeStart = static_cast<std::uint64_t>(run.lcn) * geometry.bytesPerCluster};
            const std::uint64_t first = (bytes.streamStart + rs - 1) / rs;
            const std::uint64_t end = std::min(bytes.streamEnd / rs, recordCount);
            planner.plan(bytes, first, std::max(first, end));
            if (bytes.streamEnd % rs != 0) {
                addSplit(plan, mftRuns, geometry, bitmap, bytes.streamEnd / rs);
            }
        }
        std::ranges::sort(plan.chunks, {}, &MftChunk::volumeOffset);
        plan.skippedRecords = recordCount - std::min(recordCount, recordsRead(plan));
        return plan;
    }

    std::optional<std::vector<DataRun>> joinSegments(std::vector<const Attribute*> segments) {
        std::ranges::sort(segments, {}, &Attribute::startVcn);
        std::vector<DataRun> runs;
        std::uint64_t vcn = 0;
        for (const Attribute* segment : segments) {
            if (segment->startVcn != vcn) {
                return std::nullopt;
            }
            runs.insert(runs.end(), segment->runs.begin(), segment->runs.end());
            vcn = segment->lastVcn + 1;
        }
        return runs;
    }

    std::vector<RecordPiece> mapStreamBytes(std::span<const DataRun> runs, std::uint32_t bytesPerCluster,
                                            StreamBytes range) {
        std::vector<RecordPiece> pieces;
        std::uint64_t at = range.offset;
        const std::uint64_t end = range.offset + range.length;
        for (const DataRun& run : runs) {
            const std::uint64_t runStart = run.vcn * bytesPerCluster;
            const std::uint64_t runEnd = runStart + run.length * bytesPerCluster;
            if (at >= end || runEnd <= at || runStart > at) {
                continue;
            }
            if (run.hole()) {
                return {};
            }
            const std::uint64_t take = std::min(end, runEnd) - at;
            pieces.push_back({static_cast<std::uint64_t>(run.lcn) * bytesPerCluster + (at - runStart),
                              static_cast<std::uint32_t>(take)});
            at += take;
        }
        return at == end ? pieces : std::vector<RecordPiece>{};
    }

} // namespace icd::core::ntfs
