#include "core/snapshot/Snapshot.h"

#include <cstdint>

namespace icd::core {

    std::size_t snapshotMemoryBytes(const Snapshot& snapshot) {
        const std::size_t files = snapshot.files.size();
        // Per file: the columns, its start in the per-file extent list, its child-list start and its slot in
        // its parent's child list.
        const std::size_t perFile = FileTable::bytesPerFile + 3 * sizeof(std::uint32_t);
        // Per extent: the record and its position in the per-file list.
        const std::size_t perExtent = sizeof(ExtentRecord) + sizeof(std::uint32_t);
        return files * perFile + snapshot.extents.size() * perExtent + snapshot.names.textBytes() +
            snapshot.names.size() * sizeof(std::uint32_t) + snapshot.freeSpace.size() * sizeof(ClusterRun);
    }

} // namespace icd::core
