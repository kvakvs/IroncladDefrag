#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "core/model/VolumeLayout.h"
#include "core/snapshot/Snapshot.h"
#include "platform/win/UsnJournal.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icd::services {

    // What a catch-up did.
    enum class CatchUpOutcome : std::uint8_t {
        UpToDate, // Nothing was journaled since the snapshot: it stands as it is.
        CaughtUp, // The changed files were read again and the snapshot updated.
        RescanNeeded, // The journal cannot bring the snapshot up to date (see reason): scan the volume.
    };

    // The result of a catch-up.
    struct CatchUpResult {
        CatchUpOutcome outcome = CatchUpOutcome::RescanNeeded; // What happened.
        std::string reason; // Why a rescan is needed; empty otherwise.
        core::Snapshot snapshot; // The up-to-date snapshot (UpToDate and CaughtUp).
        std::vector<core::ClusterRun> dirty; // LCN ranges whose map summaries may have changed, sorted and merged.
        std::uint64_t journalRecords = 0; // Change records read.
        std::uint64_t changedRecords = 0; // Distinct MFT records they named (files and their directories).
        std::uint64_t recordsRead = 0; // Records read again, metadata included.
    };

    // Why a saved snapshot cannot be caught up with the volume as it is now, or nullopt when it can (plan.md 6.2
    // principle 6): no cursor in the snapshot, no journal on the volume, another journal id (deleted and recreated),
    // records since the cursor already purged (the journal wrapped), or a volume of another size.
    [[nodiscard]] std::optional<std::string> catchUpBlocker(const core::Snapshot& saved,
                                                            const std::optional<platform::UsnJournalState>& journal,
                                                            core::VolumeGeometry current);

    // Brings a saved snapshot (made by the MFT scanner) up to date from the USN journal: reads the records from the
    // saved cursor to the current end, re-reads the MFT records of every file they name, of those files' directories
    // (their indexes may have grown) and of the NTFS metadata files (whose changes are not journaled: $MFT, $UsnJrnl,
    // $Secure, ...), with their extension records, through the MFT pipeline, and finishes the kept and the re-read
    // files exactly like a full scan. Free space: the saved free runs plus the clusters the re-read files had, minus
    // the clusters they have now. Reads only. Throws like the MFT scanner and OperationCancelled on cancel.
    [[nodiscard]] CatchUpResult catchUpSnapshot(HANDLE volume, const std::wstring& volumeName, core::Snapshot saved,
                                                const core::NtfsLayout& ntfs, core::VolumeGeometry geometry,
                                                core::Executor& executor, const core::CancelToken& token,
                                                core::ProgressSink* progress = nullptr);

} // namespace icd::services
