#include "services/scan/MftSecondPass.h"

#include "core/ntfs/AttributeList.h"
#include "core/ntfs/NtfsFormat.h"

#include <algorithm>
#include <exception>
#include <optional>

namespace icd::services::mft {

    namespace {

        using core::ntfs::AttributeListEntry;
        using core::ntfs::AttributeType;
        using core::ntfs::DataRun;

        // Base rows per chunk of the parallel loop (lists are rare and small).
        constexpr std::size_t listGrain = 16;

        // What the pass found for one base row.
        struct Joined {
            std::vector<std::size_t> parts; // Accepted extension rows in record order.
            bool nonResident = false; // The list was read from clusters.
            bool bad = false; // The list could not be read or parsed.
        };

        // Base rows that carry an attribute list.
        std::vector<std::size_t> listRows(const RowIndex& rows) {
            std::vector<std::size_t> out;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const RecordRow& row = rows.row(i);
                if (row.baseReference == 0 && (row.facts & RowAttributeList) != 0) {
                    out.push_back(i);
                }
            }
            return out;
        }

        // The runs of a row's non-resident segment, from the chunk's provisional extents (stream = segment index).
        std::vector<DataRun> segmentRuns(const ChunkRows& chunk, const RecordRow& row, std::uint16_t segment) {
            std::vector<DataRun> runs;
            for (std::uint32_t k = row.firstExtent; k < row.firstExtent + row.extentCount; ++k) {
                const core::ExtentEntry& e = chunk.extents[k];
                if (e.stream == segment) {
                    runs.push_back({e.vcn, static_cast<std::int64_t>(e.run.start), e.run.length});
                }
            }
            return runs;
        }

        // The list's bytes: the resident value, or its clusters read through readList.
        std::vector<std::byte> listBytes(const ChunkRows& chunk, const RecordRow& row, std::uint16_t s,
                                         const ListReader& readList) {
            const SegmentRow& seg = chunk.segments[row.firstSegment + s];
            if (!seg.nonResident) {
                const auto begin = chunk.values.begin() + seg.valueOffset;
                return {begin, begin + seg.valueBytes};
            }
            return readList(segmentRuns(chunk, row, s), seg.dataSize);
        }

        // The entries of a base row's attribute list; nullopt when it cannot be read or parsed.
        std::optional<std::vector<AttributeListEntry>> entriesOf(const RowIndex& rows, std::size_t i,
                                                                 const ListReader& readList, Joined& joined) {
            const ChunkRows& chunk = rows.chunk(i);
            const RecordRow& row = rows.row(i);
            for (std::uint16_t s = 0; s < row.segmentCount; ++s) {
                if (chunk.segments[row.firstSegment + s].type != AttributeType::AttributeList) {
                    continue;
                }
                joined.nonResident = chunk.segments[row.firstSegment + s].nonResident;
                try {
                    std::vector<AttributeListEntry> entries;
                    return core::ntfs::parseAttributeList(listBytes(chunk, row, s, readList), entries)
                        ? std::optional(std::move(entries))
                        : std::nullopt;
                }
                catch (const std::exception&) {
                    return std::nullopt; // Unreadable clusters: the file keeps what its base record holds.
                }
            }
            return std::nullopt;
        }

        // Accepts the extension records a base row's list names.
        Joined join(const RowIndex& rows, std::size_t i, const ListReader& readList) {
            Joined joined;
            const auto entries = entriesOf(rows, i, readList, joined);
            if (!entries) {
                joined.bad = true;
                return joined;
            }
            const RecordRow& base = rows.row(i);
            std::vector<std::uint64_t> references;
            for (const AttributeListEntry& e : *entries) {
                if (core::ntfs::recordOf(e.recordReference) != base.record()) {
                    references.push_back(e.recordReference);
                }
            }
            std::ranges::sort(references, {}, [](std::uint64_t r) { return core::ntfs::recordOf(r); });
            references.erase(std::ranges::unique(references).begin(), references.end());
            for (const std::uint64_t reference : references) {
                const std::size_t r = rows.find(core::ntfs::recordOf(reference));
                const bool current = r != RowIndex::none && rows.row(r).reference == reference &&
                    rows.row(r).baseReference == base.reference;
                if (current) {
                    joined.parts.push_back(r);
                }
            }
            return joined;
        }

        // Extension rows in the scan.
        std::uint64_t extensionRows(const RowIndex& rows) {
            std::uint64_t count = 0;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                count += rows.row(i).baseReference != 0 ? 1 : 0;
            }
            return count;
        }

    } // namespace

    ExtensionMap joinExtensionRecords(const RowIndex& rows, const ListReader& readList, core::Executor& executor,
                                      SecondPassStats& stats, const core::CancelToken& token) {
        const std::vector<std::size_t> bases = listRows(rows);
        std::vector<Joined> joined(bases.size());
        executor.parallelFor(
            {bases.size(), listGrain},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t k = begin; k < end; ++k) {
                    joined[k] = join(rows, bases[k], readList);
                }
            },
            token);
        ExtensionMap out;
        stats = {};
        stats.listFiles = bases.size();
        for (std::size_t k = 0; k < bases.size(); ++k) {
            stats.nonResidentLists += joined[k].nonResident ? 1 : 0;
            stats.badLists += joined[k].bad ? 1 : 0;
            stats.accepted += joined[k].parts.size();
            if (!joined[k].parts.empty()) {
                out.emplace(bases[k], std::move(joined[k].parts));
            }
        }
        stats.rejected = extensionRows(rows) - stats.accepted;
        return out;
    }

} // namespace icd::services::mft
