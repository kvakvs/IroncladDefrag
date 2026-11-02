#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "services/workflow/FakeResults.h"

#include <cstdint>
#include <span>
#include <vector>

namespace icd::services {

    // A snapshot's files classified by the built-in catalog (plan.md 8.2), for the class color mode and the Summary
    // tab.
    struct Classification {
        std::vector<std::uint8_t> classes; // Per file: its ClassId.
        std::vector<ClassSummary> summary; // Per class with files, in catalog order: files and bytes on disk.
    };

    // Classifies every file of a snapshot with the built-in catalog. `churn` (per file, empty when unknown) comes from
    // the USN journal (churnFlags()); `now` is the FILETIME ages are measured from (the scan time). The same for any
    // executor.
    [[nodiscard]] Classification classifyVolume(const core::Snapshot& snapshot, std::span<const std::uint8_t> churn,
                                                std::int64_t now, core::Executor& executor,
                                                const core::CancelToken& token = {});

    // Files and allocated bytes per class (classes without files left out), counted in parallel; the same for any
    // executor.
    [[nodiscard]] std::vector<ClassSummary> classSummary(const core::Snapshot& snapshot,
                                                         std::span<const std::uint8_t> classes,
                                                         core::Executor& executor, const core::CancelToken& token = {});

} // namespace icd::services
