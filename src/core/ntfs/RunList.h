#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::core::ntfs {

    // One mapping pair of a non-resident attribute: clusters [vcn, vcn + length) of the stream lie at
    // [lcn, lcn + length) on the volume, or nowhere for a hole.
    struct DataRun {
        std::uint64_t vcn = 0; // First cluster inside the stream.
        std::int64_t lcn = -1; // First cluster on the volume; -1 for a hole (sparse or compressed away).
        std::uint64_t length = 0; // Clusters.

        // Whether the run takes no clusters on disk.
        [[nodiscard]] constexpr bool hole() const { return lcn < 0; }

        // Runs compare as values (tests).
        friend constexpr bool operator==(const DataRun&, const DataRun&) = default;
    };

    // Decodes an attribute's mapping pairs (the run list) and appends the runs to `out`. Each pair has a header
    // byte with the sizes of a length and of an LCN delta from the previous run; a pair without a delta is a
    // hole, and a zero byte ends the list. The runs must cover exactly [startVcn, lastVcn]; an empty attribute
    // has lastVcn = startVcn - 1. Returns false and leaves `out` as it was when the list is malformed: sizes
    // over 8 bytes, a zero length, an LCN below zero, overflow, no terminator or the wrong coverage.
    [[nodiscard]] bool decodeRunList(std::span<const std::byte> bytes, std::uint64_t startVcn, std::uint64_t lastVcn,
                                     std::vector<DataRun>& out);

} // namespace icd::core::ntfs
