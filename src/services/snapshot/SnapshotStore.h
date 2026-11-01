#pragma once

#include "core/exec/Executor.h"
#include "core/snapshot/SnapshotIO.h"

#include <filesystem>
#include <optional>
#include <string>

namespace icd::services {

    // Saved snapshots on disk, one file per volume (plan.md 6.2 principle 6): written after an analysis so the next
    // run can catch up from the USN journal, compressed with XPRESS. Saving writes a temporary file and replaces the
    // old one, so a crash mid-save leaves the previous snapshot intact.
    class SnapshotStore {
    public:
        // A store in a directory (created when saving).
        explicit SnapshotStore(std::filesystem::path directory);

        // %LOCALAPPDATA%\IroncladDefrag\snapshots, beside the logs.
        [[nodiscard]] static std::filesystem::path defaultDirectory();

        // File of a volume: its GUID ("Volume{...}.icds"), or a sanitised name for other volume names.
        [[nodiscard]] std::filesystem::path pathFor(const std::string& volumeName) const;

        // Saves (replaces) the volume's snapshot; meta.volumeName names the file. Throws on I/O errors.
        void save(const core::Snapshot& snapshot, const core::SnapshotMeta& meta, core::Executor& executor) const;

        // The meta of the saved snapshot without loading it; nullopt when there is none or it cannot be read.
        [[nodiscard]] std::optional<core::SnapshotMeta> peek(const std::string& volumeName) const;

        // Loads the saved snapshot. Returns nullopt when there is none, it is damaged or of another format version;
        // `why` (optional) then says which, for the log. A damaged file is never half-loaded.
        [[nodiscard]] std::optional<core::LoadedSnapshot> load(const std::string& volumeName, core::Executor& executor,
                                                               std::string* why = nullptr) const;

        // Deletes the volume's snapshot, if any (for example after the volume was reformatted).
        void remove(const std::string& volumeName) const;

    private:
        std::filesystem::path directory_; // Where the files are.
    };

} // namespace icd::services
