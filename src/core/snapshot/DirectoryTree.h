#pragma once

#include "core/snapshot/FileTable.h"
#include "core/snapshot/NamePool.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace icd::core {

    // The directory structure as parent indexes (in FileTable) plus a child list per directory, instead of a
    // full path per file (plan.md 6.2 principle 4). Paths are rebuilt on demand for tooltips and logs.
    class DirectoryTree {
    public:
        // An empty tree.
        DirectoryTree() = default;

        // Builds child lists from the parent column; children keep FileIndex order. Throws SnapshotError for a
        // parent index outside the table.
        explicit DirectoryTree(const FileTable& files);

        // Direct children of a file (empty for files that are not directories).
        [[nodiscard]] std::span<const FileIndex> children(FileIndex file) const;

        // Files without a parent (the volume root, and orphans of a damaged volume).
        [[nodiscard]] std::span<const FileIndex> roots() const { return roots_; }

        // Path from the root, joined with backslashes and without the root's own name: "\Photos\a.jpg". The root
        // itself is "\". A cycle in damaged data cuts the walk: the path then starts with "\...".
        [[nodiscard]] static std::string path(FileIndex file, const FileTable& files, const NamePool& names);

    private:
        std::vector<std::uint32_t> childStart_; // Per file: start in children_; one extra end marker.
        std::vector<FileIndex> children_; // Child indexes grouped by parent.
        std::vector<FileIndex> roots_; // Files without a parent.
    };

} // namespace icd::core
