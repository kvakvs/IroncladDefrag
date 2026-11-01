#pragma once

#include "core/snapshot/Snapshot.h"
#include "services/scan/ScanStats.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icd::services {

    // Outcome of comparing two scans: counts and the first differences in words.
    struct ScanComparison {
        std::uint64_t filesCompared = 0; // Files present in both and compared field by field.
        std::uint64_t runsCompared = 0; // Runs (joined where they continue on disk) compared.
        std::uint64_t differences = 0; // Everything that did not match.
        std::uint64_t extraMetadataFiles = 0; // NTFS metadata only the MFT scan lists (not a difference).
        std::uint64_t deferredAccessTimes = 0; // Access times newer in memory than in the record (not a difference).
        std::vector<std::string> samples; // The first differences, for the report.

        // Whether nothing differed.
        [[nodiscard]] bool same() const { return differences == 0; }
    };

    // The MFT scan against the API scan as the reference (plan.md 10.2: "it must give the same extent index"). Files
    // are matched by MFT record. For every file the API path could read: name, parent, size, flags, creation and
    // modification times and the runs of each data stream and directory index must be equal; the access time may only
    // be older in the record (NTFS writes it lazily). The free runs must be equal. Files the API path could not
    // open, attributes it cannot see (stream ordinals from 0x8000), metadata files it does not list and its
    // `$Unattributed` placeholder are not compared one by one: the clusters left after the matched runs and free space
    // must then be the same total.
    [[nodiscard]] ScanComparison compareWithApi(const core::Snapshot& api, const ScanStats& apiStats,
                                                const core::Snapshot& mft);

    // What compareExact() leaves out.
    struct ExactOptions {
        // Compare last-access times. A USN catch-up cannot: NTFS does not journal access-time updates.
        bool accessTimes = true;
    };

    // Exact equality of two snapshots (two runs of one scanner with different worker counts, plan.md 6.2 principle
    // 8, or a catch-up against a full rescan): geometry, every file column and name, every extent record, the per-file
    // order and the free runs.
    [[nodiscard]] ScanComparison compareExact(const core::Snapshot& a, const core::Snapshot& b,
                                              const ExactOptions& options = {});

} // namespace icd::services
