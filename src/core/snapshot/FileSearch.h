#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace icd::core {

    // How well a file's name matches a search; results are ordered best first.
    enum class SearchRank : std::uint8_t {
        Exact, // The name equals the query's name part.
        Prefix, // The name starts with it.
        Contains, // The name contains it (queries without a backslash only).
    };

    // One file a search found.
    struct SearchHit {
        FileIndex file = noFile; // The file.
        SearchRank rank = SearchRank::Contains; // How well it matched.

        // Hits compare as values (tests).
        friend constexpr bool operator==(const SearchHit&, const SearchHit&) = default;
    };

    // What searchFiles() found.
    struct SearchResult {
        std::vector<SearchHit> hits; // Best rank first, then file index order; at most the limit.
        std::size_t total = 0; // Every match, also those past the limit.
    };

    // Finds files for "go to file" (plan.md 7.4). A query without a backslash matches file names that contain
    // it; with a backslash it matches paths that contain it ("photos\a" finds "\Archive\Photos\a.jpg"), so the
    // part after the last backslash must start the name and the part before it must end the parent's path.
    // ASCII letters match regardless of case, other characters exactly; '/' counts as '\'. Each distinct name
    // of the pool is tested once, then the file column is scanned; both in parallel, with a result that does
    // not depend on the thread count. An empty query finds nothing. Throws OperationCancelled.
    [[nodiscard]] SearchResult searchFiles(const Snapshot& snapshot, std::string_view query, std::size_t limit,
                                           Executor& executor, const CancelToken& token = {});

} // namespace icd::core
