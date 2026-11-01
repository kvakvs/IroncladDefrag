#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/ntfs/RunList.h"
#include "services/scan/MftMerge.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace icd::services::mft {

    // Reads the bytes [0, size) of a non-resident attribute list through its runs (VCN order). The scanner reads
    // them from the volume; tests hand in bytes. Throws when they cannot be read.
    using ListReader =
        std::function<std::vector<std::byte>(std::span<const core::ntfs::DataRun> runs, std::uint64_t size)>;

    // What the second pass found.
    struct SecondPassStats {
        std::uint64_t listFiles = 0; // Base records with an $ATTRIBUTE_LIST.
        std::uint64_t nonResidentLists = 0; // Lists read from clusters.
        std::uint64_t badLists = 0; // Lists that could not be read or parsed (their extension records stay unused).
        std::uint64_t accepted = 0; // Extension records joined to their base file.
        std::uint64_t rejected = 0; // In-use extension records no current list names (stale, or of damaged files).
    };

    // The second, smaller pass of plan.md 6.3 over files whose attributes spread over several records (many
    // fragments, many hard links, a very fragmented $MFT). For every base row with an $ATTRIBUTE_LIST it parses the
    // list (resident value, or read through `readList`) and accepts each listed extension record that is in use, names
    // this base record (same record and sequence) and has the sequence number the list names. Unlisted or stale
    // extension records are rejected, so a reused record never adds clusters to the wrong file. Runs on the executor
    // per base row; the result is the same for any executor.
    [[nodiscard]] ExtensionMap joinExtensionRecords(const RowIndex& rows, const ListReader& readList,
                                                    core::Executor& executor, SecondPassStats& stats,
                                                    const core::CancelToken& token = {});

    // Records that the attribute lists of the rows name but that are not among the rows: the extension records a
    // re-read of chosen files (USN catch-up) still has to read. Sorted, without duplicates; unreadable lists name none.
    [[nodiscard]] std::vector<std::uint64_t> missingListedRecords(const RowIndex& rows, const ListReader& readList);

} // namespace icd::services::mft
