#include "core/snapshot/FileSearch.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>

namespace icd::core {

    namespace {

        // Names per chunk of the name pass.
        constexpr std::size_t nameGrain = 16384;

        // Files per chunk of the file pass.
        constexpr std::size_t fileGrain = 65536;

        // Rank of a name that does not match.
        constexpr std::uint8_t noMatch = 0xFF;

        // Number of ranks (SearchRank values).
        constexpr std::size_t rankCount = 3;

        // Deepest parent chain walked before assuming a cycle in damaged data (as DirectoryTree::path()).
        constexpr std::size_t maxDepth = 16384;

        // The parsed query, lowercased.
        struct Query {
            std::string name; // Part after the last backslash: matched against file names.
            std::string directory; // Part before it: must end the parent's path.
            bool hasDirectory = false; // Whether the query held a backslash.
        };

        // ASCII lowercase of one character; other bytes unchanged (UTF-8 sequences stay intact).
        char fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

        // Whether text equals folded (already lowercase), ignoring ASCII case in text.
        bool equalsFolded(std::string_view text, std::string_view folded) {
            return text.size() == folded.size() &&
                std::equal(text.begin(), text.end(), folded.begin(), [](char a, char b) { return fold(a) == b; });
        }

        // Whether text contains folded, ignoring ASCII case in text.
        bool containsFolded(std::string_view text, std::string_view folded) {
            if (folded.size() > text.size()) {
                return false;
            }
            for (std::size_t at = 0; at + folded.size() <= text.size(); ++at) {
                if (fold(text[at]) == folded.front() && equalsFolded(text.substr(at, folded.size()), folded)) {
                    return true;
                }
            }
            return false;
        }

        // The query cleaned up: trimmed, '/' as '\', lowercased, trailing backslashes dropped; nullopt when
        // nothing is left to match.
        std::optional<Query> parseQuery(std::string_view text) {
            const auto first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos) {
                return std::nullopt;
            }
            std::string clean(text.substr(first, text.find_last_not_of(" \t") - first + 1));
            std::ranges::transform(clean, clean.begin(), [](char c) { return c == '/' ? '\\' : fold(c); });
            while (!clean.empty() && clean.back() == '\\') {
                clean.pop_back();
            }
            if (clean.empty()) {
                return std::nullopt;
            }
            const auto split = clean.rfind('\\');
            if (split == std::string::npos) {
                return Query{.name = clean, .directory = {}, .hasDirectory = false};
            }
            return Query{.name = clean.substr(split + 1), .directory = clean.substr(0, split), .hasDirectory = true};
        }

        // The rank of a name for a query, or noMatch. After a backslash the name must start with the name part.
        std::uint8_t rankName(std::string_view name, const Query& query) {
            if (equalsFolded(name, query.name)) {
                return static_cast<std::uint8_t>(SearchRank::Exact);
            }
            if (name.size() >= query.name.size() && equalsFolded(name.substr(0, query.name.size()), query.name)) {
                return static_cast<std::uint8_t>(SearchRank::Prefix);
            }
            if (!query.hasDirectory && containsFolded(name, query.name)) {
                return static_cast<std::uint8_t>(SearchRank::Contains);
            }
            return noMatch;
        }

        // Whether a file has no parent (noFile, or itself as NTFS stores the root).
        bool isRoot(const FileTable& files, FileIndex file) {
            const FileIndex parent = files.parent(file);
            return parent == noFile || parent == file || parent >= files.size();
        }

        // Takes one directory name off the end of rest ("...\name"); false when rest does not end that way.
        bool consumeComponent(std::string_view& rest, std::string_view name) {
            if (rest.size() <= name.size()) {
                return false;
            }
            if (!equalsFolded(name, rest.substr(rest.size() - name.size())) ||
                rest[rest.size() - name.size() - 1] != '\\') {
                return false;
            }
            rest.remove_suffix(name.size() + 1);
            return true;
        }

        // Whether the path of a file's parent ends with suffix (lowercase), compared name by name up the parent
        // chain, so no path strings are built. The root's path is "\", its children's paths start with "\".
        bool parentPathEndsWith(const Snapshot& snapshot, FileIndex file, std::string_view suffix) {
            const FileTable& files = snapshot.files;
            if (isRoot(files, file)) {
                return false;
            }
            std::string_view rest = suffix;
            FileIndex current = files.parent(file);
            for (std::size_t depth = 0; depth < maxDepth && !rest.empty(); ++depth) {
                if (isRoot(files, current)) {
                    return rest == "\\";
                }
                const std::string_view name = snapshot.names.name(files.name(current));
                if (rest.size() <= name.size()) {
                    return equalsFolded(name.substr(name.size() - rest.size()), rest);
                }
                if (!consumeComponent(rest, name)) {
                    return false;
                }
                current = files.parent(current);
            }
            return rest.empty();
        }

        // Matches of one chunk of files (or of several, combined in chunk order).
        struct Partial {
            std::array<std::vector<FileIndex>, rankCount>
                best; // Per rank: first matches in file order, up to the limit.
            std::size_t total = 0; // All matches.
        };

        // The rank of every distinct name, by NameId.
        std::vector<std::uint8_t> rankNames(const NamePool& names, const Query& query, Executor& executor,
                                            const CancelToken& token) {
            std::vector<std::uint8_t> ranks(names.size(), noMatch);
            executor.parallelFor(
                {.count = names.size(), .grain = nameGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t id = begin; id < end; ++id) {
                        ranks[id] = rankName(names.name(static_cast<NameId>(id)), query);
                    }
                },
                token);
            return ranks;
        }

        // Appends from to into, keeping at most limit entries.
        void appendLimited(std::vector<FileIndex>& into, const std::vector<FileIndex>& from, std::size_t limit) {
            const std::size_t room = limit - std::min(limit, into.size());
            into.insert(into.end(), from.begin(),
                        from.begin() + static_cast<std::ptrdiff_t>(std::min(room, from.size())));
        }

        // Everything one search needs while scanning files.
        struct Scan {
            const Snapshot& snapshot; // Searched.
            const Query& query; // What for.
            const std::vector<std::uint8_t>& ranks; // Rank per name.
            std::size_t limit = 0; // Hits kept per rank.
        };

        // Matches among the files [begin, end).
        Partial scanFiles(const Scan& scan, std::size_t begin, std::size_t end) {
            Partial partial;
            for (std::size_t index = begin; index < end; ++index) {
                const auto file = static_cast<FileIndex>(index);
                const std::uint8_t rank = scan.ranks[scan.snapshot.files.name(file)];
                if (rank == noMatch ||
                    (scan.query.hasDirectory && !parentPathEndsWith(scan.snapshot, file, scan.query.directory))) {
                    continue;
                }
                ++partial.total;
                if (partial.best.at(rank).size() < scan.limit) {
                    partial.best.at(rank).push_back(file);
                }
            }
            return partial;
        }

    } // namespace

    SearchResult searchFiles(const Snapshot& snapshot, std::string_view query, std::size_t limit, Executor& executor,
                             const CancelToken& token) {
        const auto parsed = parseQuery(query);
        if (!parsed) {
            return {};
        }
        const std::vector<std::uint8_t> ranks = rankNames(snapshot.names, *parsed, executor, token);
        const Scan scan{.snapshot = snapshot, .query = *parsed, .ranks = ranks, .limit = limit};
        const Partial found = parallelReduce(
            executor, {.count = snapshot.files.size(), .grain = fileGrain}, Partial{},
            [&scan](std::size_t begin, std::size_t end) { return scanFiles(scan, begin, end); },
            [limit](Partial into, Partial from) {
                for (std::size_t rank = 0; rank < rankCount; ++rank) {
                    appendLimited(into.best.at(rank), from.best.at(rank), limit);
                }
                into.total += from.total;
                return into;
            },
            token);
        SearchResult result;
        result.total = found.total;
        for (std::size_t rank = 0; rank < rankCount && result.hits.size() < limit; ++rank) {
            for (const FileIndex file : found.best.at(rank)) {
                if (result.hits.size() == limit) {
                    break;
                }
                result.hits.push_back({.file = file, .rank = static_cast<SearchRank>(rank)});
            }
        }
        return result;
    }

} // namespace icd::core
