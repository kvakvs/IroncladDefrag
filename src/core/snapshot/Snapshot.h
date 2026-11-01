#pragma once

#include "core/snapshot/DirectoryTree.h"
#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/FileTable.h"
#include "core/snapshot/FreeRunList.h"
#include "core/snapshot/NamePool.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace icd::core {

    // Where the volume's USN change journal stood when a scan started (plan.md 6.2 principle 6): a later run
    // reads the journal from here instead of rescanning, unless the journal ID changed.
    struct UsnCursor {
        std::uint64_t journalId = 0; // Journal instance; a different ID means the journal was recreated.
        std::int64_t nextUsn = 0; // First USN not yet reflected in the snapshot.
    };

    // Everything known about a volume's layout at one point in time: files, names, directories, extents
    // and free space. The map, classification and the planner all read it; scanners and the synthetic
    // generator produce it through SnapshotBuilder. Immutable once built.
    struct Snapshot {
        VolumeGeometry geometry; // Cluster size and count.
        NamePool names; // File names.
        FileTable files; // Per-file columns.
        DirectoryTree tree; // Children per directory.
        ExtentIndex extents; // Extents by LCN and by file.
        FreeRunList freeSpace; // Free runs.
        std::optional<UsnCursor> usn; // Journal position at scan time; empty without a journal or for synthetic data.

        // Path of a file from the volume root ("\Photos\a.jpg").
        [[nodiscard]] std::string path(FileIndex file) const { return DirectoryTree::path(file, files, names); }
    };

    // Bytes the snapshot's data takes (element counts times sizes, not counting allocator slack), for the
    // memory budget checks of plan.md 6.6 and the synthetic benchmark.
    [[nodiscard]] std::size_t snapshotMemoryBytes(const Snapshot& snapshot);

} // namespace icd::core
