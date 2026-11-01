#pragma once

#include "core/job/CancelToken.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace icd::platform {

    // State of a volume's USN change journal (FSCTL_QUERY_USN_JOURNAL). A snapshot records the journal ID
    // and next USN so a later run can catch up from the journal instead of rescanning (plan.md 6.2).
    struct UsnJournalState {
        std::uint64_t journalId = 0; // Changes when the journal is deleted and recreated: then rescan.
        std::int64_t firstUsn = 0; // Oldest record still in the journal.
        std::int64_t nextUsn = 0; // USN the next change will get; the catch-up cursor.
        std::int64_t lowestValidUsn = 0; // Records below this were lost (journal wrapped).
        std::uint64_t maximumSize = 0; // Journal size limit in bytes.
    };

    // The journal state, or nullopt when the volume has no active journal (ERROR_JOURNAL_NOT_ACTIVE). Analysis
    // never creates one: that would write to the volume. Throws std::runtime_error on other failures.
    [[nodiscard]] std::optional<UsnJournalState> queryUsnJournal(HANDLE volume);

    // One MFT file record as FSCTL_ENUM_USN_DATA reports it: enough to name the file and place it in the tree
    // before its extents are read.
    struct UsnFileEntry {
        std::uint64_t fileReference = 0; // MFT record number (low 48 bits) and sequence number.
        std::uint64_t parentReference = 0; // Directory holding the name below.
        std::uint32_t attributes = 0; // FILE_ATTRIBUTE_* bits.
        std::string name; // One of the file's names (hard links have several), UTF-8.
    };

    // MFT record number of a file reference (the sequence number in the top 16 bits removed).
    [[nodiscard]] constexpr std::uint64_t mftRecordOf(std::uint64_t fileReference) {
        return fileReference & 0x0000'FFFF'FFFF'FFFFULL;
    }

    // Enumerates every in-use file record of the volume, in MFT record order, in batches of about one output
    // buffer (64 KB) each. Works without an active journal. The volume handle needs read access
    // (administrators). Throws std::runtime_error on failure and OperationCancelled between batches.
    void enumerateUsnFiles(HANDLE volume, const std::function<void(std::span<const UsnFileEntry>)>& onBatch,
                           const core::CancelToken& token = {});

} // namespace icd::platform
