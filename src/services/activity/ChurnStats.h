#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "platform/win/UsnJournal.h"

#include <windows.h>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace icd::services {

    // Change events of one file, or of the files in one directory (plan.md 8.2: USN journal churn). An event is one
    // open-to-close session as the journal's close record sums it up, so a file written in a thousand small appends
    // between one open and its close counts once.
    struct ChurnCounts {
        std::uint32_t creates = 0; // Sessions that created the file.
        std::uint32_t deletes = 0; // Sessions that deleted it.
        std::uint32_t overwrites = 0; // Sessions that changed data (overwrite, extend, truncate) of an existing file.

        // All events.
        [[nodiscard]] std::uint64_t total() const { return std::uint64_t{creates} + deletes + overwrites; }
    };

    // Churn over the journal's retained window: per file reference, and per directory reference for the files directly
    // in it (deleted files included, which the snapshot no longer has).
    struct ChurnStats {
        std::unordered_map<std::uint64_t, ChurnCounts> files; // By file reference.
        std::unordered_map<std::uint64_t, ChurnCounts> directories; // By directory reference: its children's events.
        std::uint64_t records = 0; // Journal records read.
        std::uint64_t sessions = 0; // Close records counted.
        std::int64_t firstUsn = 0; // Start of the window read.
        std::int64_t nextUsn = 0; // End of the window read.

        // Adds journal records. Creations and data changes count from close records (their reason bits sum up the
        // session); a delete counts from its first record, since its close record waits for every other handle (a
        // scanner's) to close. A delete counts for the directory the file was last seen in: when another handle is
        // open, Windows moves the file into \$Extend\$Deleted first, and the delete's records name that directory.
        void add(std::span<const platform::UsnChange> changes);

    private:
        // The directory a session's events belong to.
        [[nodiscard]] std::uint64_t directoryOf(const platform::UsnChange& c) const;

        std::unordered_map<std::uint64_t, std::uint64_t> homes_; // File -> its directory before any delete.
        std::unordered_set<std::uint64_t> deleted_; // Files whose delete was counted (once per file reference).
    };

    // When a file or directory counts as churn. A directory with fewer deletes (at the same share) churns when most of
    // its sibling directories do.
    struct ChurnOptions {
        std::uint32_t fileEvents = 10; // A file with this many events churns (rewritten all the time).
        std::uint32_t directoryDeletes = 10; // A directory whose files were deleted this often...
        std::uint32_t deleteSharePercent = 25; // ...and at least this share of its creations, churns.
    };

    // Reads the whole retained journal of a volume (from the lowest valid USN to the next one) into churn counts.
    // Empty counts when the volume has no journal. Reads only. Throws std::runtime_error on read failures and
    // OperationCancelled on cancel.
    [[nodiscard]] ChurnStats collectChurn(HANDLE volume, const core::CancelToken& token = {});

    // Whether a directory's children churn: many of the files created there are deleted again (build output, temp).
    [[nodiscard]] bool directoryChurns(const ChurnCounts& children, const ChurnOptions& options);

    // Per file of a snapshot: 1 when it churns itself, lies in a churning directory or below one; 0 otherwise. Feeds
    // the rule engine (ClassifyInputs::churn). The same for any executor.
    [[nodiscard]] std::vector<std::uint8_t> churnFlags(const core::Snapshot& snapshot, const ChurnStats& stats,
                                                       const ChurnOptions& options, core::Executor& executor,
                                                       const core::CancelToken& token = {});

    // Paths of the churning directories of a snapshot, sorted (for reports and checks).
    [[nodiscard]] std::vector<std::string> churnDirectories(const core::Snapshot& snapshot, const ChurnStats& stats,
                                                            const ChurnOptions& options);

} // namespace icd::services
