#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace icd::core::classify {

    // What a snapshot's timestamps say about last-access updates (plan.md 8.2: look for evidence in the timestamp
    // distribution). A file read after its last change shows an access time later than both its creation and its
    // modification; on a volume without updates access times stay where creation or a write put them.
    struct AccessTimeEvidence {
        std::uint64_t files = 0; // Regular files looked at (no directories, metadata or placeholders).
        std::uint64_t readAfterWrite = 0; // Accessed more than an hour after both creation and the last change.
        std::uint64_t readRecently = 0; // Of those, accessed within 30 days of the newest time in the snapshot.
    };

    // Whether access-based strategies (hot and cold zones, access-time sort) may use the access times.
    enum class AccessTimeVerdict : std::uint8_t {
        Reliable, // Updates are on and the volume shows them.
        Unreliable, // Updates are off, or the volume shows none.
        Unknown, // Not enough to tell.
    };

    // The verdict in one word ("Reliable", "Unreliable", "Unknown").
    [[nodiscard]] std::string_view verdictName(AccessTimeVerdict verdict);

    // The verdict with a sentence for the Summary tab.
    struct AccessTimeAssessment {
        AccessTimeVerdict verdict = AccessTimeVerdict::Unknown; // The decision.
        std::string reason; // Why, in plain words.
        AccessTimeEvidence evidence; // What it was based on.
    };

    // Counts the evidence over a snapshot's regular files, in parallel; the same for any executor.
    [[nodiscard]] AccessTimeEvidence gatherAccessEvidence(const Snapshot& snapshot, Executor& executor,
                                                          const CancelToken& token = {});

    // Combines the system setting (empty when unknown) with the evidence. Off means unreliable whatever the files say
    // (old accesses stay visible after updates were turned off). On with files read after their last change means
    // reliable. Without a setting the evidence alone decides: 5% or more read after writing is reliable, under 0.5%
    // unreliable. Fewer than 100 files never decide on their own.
    [[nodiscard]] AccessTimeAssessment assessAccessTimes(std::optional<bool> updatesEnabled,
                                                         const AccessTimeEvidence& evidence);

} // namespace icd::core::classify
