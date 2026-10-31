#include "core/snapshot/ExtentIndex.h"

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>

namespace icd::core {

    ExtentRecord::ExtentRecord(const Fields& fields) :
        packed_(fields.run.start | (fields.run.length << 40U)), file_(fields.file), stream_(fields.stream),
        flags_(fields.flags) {
        const ClusterRun& run = fields.run;
        if (run.start > maxExtentLcn || run.length > maxExtentLength) {
            throw SnapshotError(
                std::format("extent at LCN {} with {} clusters does not fit a record", run.start, run.length));
        }
    }

    const ExtentRecord* ExtentIndex::find(Lcn lcn) const {
        // The first record starting after lcn; the one before it is the only candidate.
        const auto after =
            std::ranges::upper_bound(records_, lcn, std::less<>{}, [](const ExtentRecord& r) { return r.lcn(); });
        if (after == records_.begin()) {
            return nullptr;
        }
        const ExtentRecord& candidate = *std::prev(after);
        return candidate.run().contains(lcn) ? &candidate : nullptr;
    }

    std::span<const ExtentRecord> ExtentIndex::overlapping(ClusterRun range) const {
        if (range.length == 0) {
            return {};
        }
        // Records are sorted and disjoint, so their ends are sorted too.
        const auto first = std::ranges::lower_bound(records_, range.start, std::less<>{},
                                                    [](const ExtentRecord& r) { return r.end() - 1; });
        const auto last = std::ranges::lower_bound(records_, range.end(), std::less<>{},
                                                   [](const ExtentRecord& r) { return r.lcn(); });
        if (first >= last) {
            return {};
        }
        return {records_.data() + (first - records_.begin()), static_cast<std::size_t>(last - first)};
    }

    std::span<const std::uint32_t> ExtentIndex::extentsOf(FileIndex file) const {
        if (file + std::size_t{1} >= fileStart_.size()) {
            return {};
        }
        const std::uint32_t begin = fileStart_[file];
        return {byFile_.data() + begin, fileStart_[file + 1] - begin};
    }

    std::size_t ExtentIndex::fragmentCount(FileIndex file) const {
        std::size_t fragments = 0;
        Lcn previousEnd = 0;
        bool first = true;
        for (const std::uint32_t position : extentsOf(file)) {
            const ExtentRecord& record = records_[position];
            if (first || record.lcn() != previousEnd) {
                ++fragments;
            }
            previousEnd = record.end();
            first = false;
        }
        return fragments;
    }

} // namespace icd::core
