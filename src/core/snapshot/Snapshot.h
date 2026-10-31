#pragma once

#include "core/snapshot/DirectoryTree.h"
#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/FileTable.h"
#include "core/snapshot/FreeRunList.h"
#include "core/snapshot/NamePool.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <string>

namespace icd::core {

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

        // Path of a file from the volume root ("\Photos\a.jpg").
        [[nodiscard]] std::string path(FileIndex file) const { return DirectoryTree::path(file, files, names); }
    };

    // Bytes the snapshot's data takes (element counts times sizes, not counting allocator slack), for the
    // memory budget checks of plan.md 6.6 and the synthetic benchmark.
    [[nodiscard]] std::size_t snapshotMemoryBytes(const Snapshot& snapshot);

} // namespace icd::core
