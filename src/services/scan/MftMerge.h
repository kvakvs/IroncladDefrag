#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "services/scan/MftRows.h"
#include "services/scan/ScanStats.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace icd::services::mft {

    // Every record row of a scan in record order, over the chunks the parser workers filled, with lookups by record
    // number. Chunks may arrive in any order (reads go in LCN order); sorting them by first record makes the row order
    // independent of how many workers parsed them (plan.md 6.2 principle 8).
    class RowIndex {
    public:
        // "No such row".
        static constexpr std::size_t none = std::numeric_limits<std::size_t>::max();

        // Sorts the chunks and indexes their rows. Throws std::runtime_error when two chunks hold the same record.
        explicit RowIndex(std::vector<std::shared_ptr<const ChunkRows>> chunks);

        // Number of rows.
        [[nodiscard]] std::size_t size() const { return refs_.size(); }

        // A row by its position in record order.
        [[nodiscard]] const RecordRow& row(std::size_t i) const { return chunk(i).records[refs_[i].row]; }

        // The chunk holding a row (for its names, segments, extents and text).
        [[nodiscard]] const ChunkRows& chunk(std::size_t i) const { return *chunks_[refs_[i].chunk]; }

        // Position of the row of a record, or none.
        [[nodiscard]] std::size_t find(std::uint64_t record) const;

        // The chunks in record order.
        [[nodiscard]] const std::vector<std::shared_ptr<const ChunkRows>>& chunks() const { return chunks_; }

        // Position in record order of a chunk's first row.
        [[nodiscard]] std::size_t chunkRowBase(std::size_t chunk) const { return rowBase_[chunk]; }

        // Index over all chunks of a row's first segment: segments numbered across the scan.
        [[nodiscard]] std::size_t segmentBase(std::size_t i) const {
            return segmentBase_[refs_[i].chunk] + row(i).firstSegment;
        }

        // Segments over all chunks.
        [[nodiscard]] std::size_t segmentCount() const { return segmentBase_.empty() ? 0 : segmentBase_.back(); }

        // Extents over all chunks.
        [[nodiscard]] std::size_t extentCount() const { return extentCount_; }

    private:
        // A row's chunk and position inside it.
        struct Ref {
            std::uint32_t chunk = 0; // Index into chunks_.
            std::uint32_t row = 0; // Index into that chunk's records.
        };

        std::vector<std::shared_ptr<const ChunkRows>> chunks_; // Sorted by first record.
        std::vector<Ref> refs_; // Rows in record order.
        std::vector<std::size_t> rowBase_; // Per chunk: position of its first row.
        std::vector<std::size_t> segmentBase_; // Per chunk: global index of its first segment; one end marker.
        std::vector<std::uint32_t> rowOfRecord_; // Record number -> position + 1 (0: no row).
        std::size_t extentCount_ = 0; // Sum over chunks.
    };

    // Extension rows that belong to a base row, as the second pass accepted them (MftSecondPass): base row position ->
    // extension row positions in record order. Empty when the scan has no files spread over several records.
    using ExtensionMap = std::unordered_map<std::size_t, std::vector<std::size_t>>;

    // What the merge produces.
    struct MergeOutput {
        core::Snapshot snapshot; // Files, extents and free space.
        ScanStats stats; // Counts for the summary.
    };

    // One file as its records (or a saved snapshot) describe it, before file indexes, parents and the flags that depend
    // on the whole volume (metadata, pinned, resident, extent flags) are known. A full scan and a USN catch-up both
    // produce drafts and finish them the same way, so their snapshots agree field by field.
    struct FileDraft {
        std::uint64_t reference = 0; // File reference (record and sequence).
        std::uint64_t parentReference = 0; // Directory of the chosen name.
        std::string_view name; // Chosen name; points into rows or a snapshot the caller keeps alive.
        std::uint64_t sizeBytes = 0; // Main stream size, or a directory's index size.
        core::FileTimes times; // From $STANDARD_INFORMATION.
        std::uint16_t flags = 0; // Attribute flags, directory, hard links, alternate streams.
        bool clusters = false; // A data stream or directory index has clusters.
        bool mainSeen = false; // The unnamed $DATA was seen (needed to call a file resident).
    };

    // Drafts in record order with their extents: `file` is the draft's position, `stream` the final ordinal, and
    // `flags` only ExtentUnmovable for attributes beyond data and directory indexes.
    struct DraftSet {
        std::vector<FileDraft> files; // Sorted by record number.
        std::vector<core::ExtentEntry> extents; // Any order.
    };

    // The drafts of rows: base records with a name (and the system records 0-11) become files in record order;
    // accepted extension rows add their names, sizes and segments to their base file. Names: the first that is not a
    // DOS alias (the root's is empty). Stream ordinals: unnamed $DATA or a directory's $I30 index is 0, named $DATA
    // streams 1, 2, ... in record order, any other non-resident attribute 0x8000 + n. Runs past the volume end are
    // dropped. The same for any executor.
    [[nodiscard]] DraftSet draftFiles(const RowIndex& rows, const ExtensionMap& extensions,
                                      core::VolumeGeometry geometry, core::Executor& executor,
                                      const core::CancelToken& token = {});

    // Finishes drafts into a snapshot: parents by reference with sequence check (unknown or stale parents: the root),
    // resolved in parallel; metadata (system records but the root, and all below them), pinned root files, residency
    // and extent flags by the shared ScanRules; extents and free runs reconciled as on the API path with the
    // `$Unattributed` placeholder last. The same for any executor (plan.md 6.2 principle 8).
    // `storage` (optional) holds the snapshot's extent index and name pool (mapped files when memory is short).
    [[nodiscard]] MergeOutput finishDrafts(DraftSet drafts, core::VolumeGeometry geometry,
                                           std::vector<core::ClusterRun> freeRuns, core::Executor& executor,
                                           const core::CancelToken& token = {},
                                           std::shared_ptr<std::pmr::memory_resource> storage = nullptr);

    // Drafts of the files of a snapshot made by the MFT scanner whose record `keep` accepts (the `$Unattributed`
    // placeholder is never kept): the flags finishDrafts() derives (metadata, unmovable, resident) are left out and
    // extent VCNs renumbered in the per-file order, so finishing them again gives the same files and extents.
    [[nodiscard]] DraftSet draftsOfSnapshot(const core::Snapshot& snapshot,
                                            const std::function<bool(std::uint64_t record)>& keep);

    // Two draft sets as one in record order (extent file positions renumbered); a record in both takes `fresh`'s.
    [[nodiscard]] DraftSet combineDrafts(DraftSet kept, DraftSet fresh);

    // The finish of a USN catch-up as a patch of the saved snapshot (core::patchSnapshot): the saved files whose
    // records were not re-read keep their names and extents where they are, `fresh` (the drafts of the re-read records,
    // sorted) replaces or adds the rest, and only the re-read files' clusters, free space and the `$Unattributed` gaps
    // are worked out again. The result equals finishing draftsOfSnapshot() of the kept files plus `fresh` with the
    // saved free runs plus the re-read files' old clusters, name ids aside (the saved pool is kept). Nullopt, with
    // `saved` untouched, when it cannot be equal (fresh extents over kept ones, a kept file whose derived flags change)
    // or the pool holds more than twice as many names as there are files: then take the full path. On success `saved`
    // is spent.
    [[nodiscard]] std::optional<core::Snapshot> finishCatchUp(core::Snapshot& saved, const DraftSet& fresh,
                                                              std::span<const std::uint64_t> rereadRecords,
                                                              core::VolumeGeometry geometry, core::Executor& executor,
                                                              const core::CancelToken& token = {});

    // The finish of a USN catch-up as a patch of the saved snapshot (core::patchSnapshot): the saved files whose
    // records were not re-read keep their names, sizes, times and extents where they are and only get their parents and
    // flags worked out again; `fresh` (the drafts of the re-read records, sorted) replaces or adds the rest; free space
    // and the
    // `$Unattributed` gaps change only around the re-read files. The result equals finishing draftsOfSnapshot() of the
    // kept files plus `fresh` with the saved free runs plus the re-read files' old clusters, name ids aside (the saved
    // pool is kept). Nullopt, with `saved` untouched, when it cannot be equal (fresh extents over kept ones, a kept
    // file whose flags change) or the pool holds more than twice as many names as there are files: then take the full
    // path. On success `saved` is spent.
    [[nodiscard]] std::optional<core::Snapshot> finishCatchUp(core::Snapshot& saved, const DraftSet& fresh,
                                                              std::span<const std::uint64_t> rereadRecords,
                                                              core::Executor& executor,
                                                              const core::CancelToken& token = {});

    // draftFiles() then finishDrafts(): the merge of a full MFT scan.
    [[nodiscard]] MergeOutput mergeRows(const RowIndex& rows, const ExtensionMap& extensions,
                                        core::VolumeGeometry geometry, std::vector<core::ClusterRun> freeRuns,
                                        core::Executor& executor, const core::CancelToken& token = {},
                                        std::shared_ptr<std::pmr::memory_resource> storage = nullptr);

    // First stream ordinal of attributes that are neither $DATA nor a directory index ($BITMAP, $ATTRIBUTE_LIST,
    // $REPARSE_POINT, $EA, $LOGGED_UTILITY_STREAM): only the MFT path sees them.
    inline constexpr std::uint16_t extraStreamBase = 0x8000;

} // namespace icd::services::mft
