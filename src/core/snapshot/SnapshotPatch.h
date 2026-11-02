#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core {

    // A new snapshot described as changes to a base one, so a USN catch-up touches only what changed instead of
    // building 20 M files again (plan.md 6.2 principle 6, 6.5: 10 s or less).
    // Per file of the new snapshot only its base file, parent and flags are given (10 bytes at 20 M files); everything
    // else of a continued file comes from the base.
    struct SnapshotPatch {
        // Per file of the new snapshot, in order: the base file it continues with the same name, size, times,
        // reference, flags and extents, or noFile for a file described in `newFiles` (a new or re-read file).
        std::span<const FileIndex> baseOf;
        std::span<const FileIndex> parents; // Per file: its final parent in the new snapshot.
        std::span<const std::uint16_t> flags; // Per file: its final flags.
        std::vector<FileEntry> newFiles; // The files without a base file, in order (their parents and flags as above).
        std::vector<ExtentEntry> extents; // Extents of the files without a base file, with final flags.
        FileIndex baseUnattributed = noFile; // The base's placeholder of used clusters nobody claims, or noFile.
        FileIndex unattributed = noFile; // The new placeholder (no base file, no extents of its own), or noFile.
    };

    // Applies a patch to `base`. Clusters of base files that are not continued become free, clusters of the base
    // placeholder stay with the new placeholder (as runs in LCN order, VCNs counted along), and the new extents take
    // theirs out of both. The result equals what reconcileScan() and SnapshotBuilder make of the same files and
    // extents with the base's free runs plus the released clusters, except that name ids are the base's (new names are
    // added to its pool). Returns nullopt, leaving `base` untouched, when the patch cannot give that result: new
    // extents overlapping each other or a continued file's extents, a continued file whose flags changed, or leftover
    // placeholder clusters without a new placeholder. On success `base` is spent. The same for any executor.
    [[nodiscard]] std::optional<Snapshot> patchSnapshot(Snapshot& base, SnapshotPatch patch, Executor& executor,
                                                        const CancelToken& token = {});

} // namespace icd::core
