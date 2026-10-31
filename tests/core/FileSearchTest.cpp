#include "core/snapshot/FileSearch.h"

#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace icd::core { namespace {

    // A small tree: "\Photos" and "\Archive\Photos" each hold an "a.jpg", plus names that rank differently for
    // "photos".
    struct SearchVolume {
        FileIndex photos = 0; // "\Photos".
        FileIndex archive = 0; // "\Archive".
        FileIndex archivePhotos = 0; // "\Archive\Photos".
        FileIndex a = 0; // "\Photos\a.jpg".
        FileIndex beach = 0; // "\Photos\Beach.JPG".
        FileIndex archivedA = 0; // "\Archive\Photos\a.jpg".
        FileIndex photosOld = 0; // "\photos-old.zip".
        FileIndex myPhotos = 0; // "\Archive\my photos.txt".
        Snapshot snapshot; // The built snapshot.
    };

    // Adds a file.
    FileIndex add(SnapshotBuilder& builder, std::string_view name, FileIndex parent, std::uint16_t flags = 0) {
        FileEntry entry;
        entry.name = name;
        entry.parent = parent;
        entry.flags = flags;
        return builder.addFile(entry);
    }

    // Builds the tree.
    SearchVolume searchVolume() {
        SearchVolume v;
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 1000});
        const FileIndex root = add(builder, "", noFile, FileDirectory);
        v.photos = add(builder, "Photos", root, FileDirectory);
        v.archive = add(builder, "Archive", root, FileDirectory);
        v.archivePhotos = add(builder, "Photos", v.archive, FileDirectory);
        v.a = add(builder, "a.jpg", v.photos);
        v.beach = add(builder, "Beach.JPG", v.photos);
        v.archivedA = add(builder, "a.jpg", v.archivePhotos);
        v.photosOld = add(builder, "photos-old.zip", root);
        v.myPhotos = add(builder, "my photos.txt", v.archive);
        v.snapshot = std::move(builder).build();
        return v;
    }

    // The files of a result, in order.
    std::vector<FileIndex> filesOf(const SearchResult& result) {
        std::vector<FileIndex> files;
        for (const SearchHit& hit : result.hits) {
            files.push_back(hit.file);
        }
        return files;
    }

    // Searches serially with a generous limit.
    SearchResult find(const SearchVolume& v, std::string_view query, std::size_t limit = 100) {
        InlineExecutor serial;
        return searchFiles(v.snapshot, query, limit, serial);
    }

    // Names match regardless of ASCII case; exact names rank first, then prefixes, then other matches.
    TEST(FileSearch, NamesByRank) {
        const SearchVolume v = searchVolume();
        const SearchResult photos = find(v, "PHOTOS");
        EXPECT_EQ(filesOf(photos), (std::vector{v.photos, v.archivePhotos, v.photosOld, v.myPhotos}));
        EXPECT_EQ(photos.total, 4U);
        ASSERT_EQ(photos.hits.size(), 4U);
        EXPECT_EQ(photos.hits[0].rank, SearchRank::Exact);
        EXPECT_EQ(photos.hits[1].rank, SearchRank::Exact);
        EXPECT_EQ(photos.hits[2].rank, SearchRank::Prefix);
        EXPECT_EQ(photos.hits[3].rank, SearchRank::Contains);

        EXPECT_EQ(filesOf(find(v, "a.jpg")), (std::vector{v.a, v.archivedA}));
        EXPECT_EQ(filesOf(find(v, "jpg")), (std::vector{v.a, v.beach, v.archivedA}));
        EXPECT_EQ(filesOf(find(v, "  beach  ")), (std::vector{v.beach})); // Spaces around are trimmed.
    }

    // With a backslash the query is part of a path: the name must start with the last part and the parent's
    // path must end with the rest.
    TEST(FileSearch, PathQueries) {
        const SearchVolume v = searchVolume();
        EXPECT_EQ(filesOf(find(v, "photos\\a")), (std::vector{v.a, v.archivedA}));
        EXPECT_EQ(filesOf(find(v, "archive\\photos\\a.jpg")), (std::vector{v.archivedA}));
        EXPECT_EQ(filesOf(find(v, "rchive\\photos\\a")), (std::vector{v.archivedA})); // Mid-name start.
        EXPECT_EQ(filesOf(find(v, "/archive/PHOTOS/a.jpg")), (std::vector{v.archivedA})); // Slashes and case.
        EXPECT_EQ(filesOf(find(v, "\\photos\\a.jpg")), (std::vector{v.a, v.archivedA})); // Contained, not anchored.
        EXPECT_EQ(filesOf(find(v, "\\archive\\photos\\")), (std::vector{v.archivePhotos})); // Trailing backslash.
        EXPECT_EQ(filesOf(find(v, "\\a.jpg")), (std::vector{v.a, v.archivedA}));
        EXPECT_EQ(filesOf(find(v, "\\photos-")), (std::vector{v.photosOld}));
        EXPECT_TRUE(find(v, "x\\photos\\a").hits.empty());
        EXPECT_TRUE(find(v, "photos\\jpg").hits.empty()); // After a backslash the name must start with it.
        EXPECT_TRUE(find(v, "archive\\a.jpg").hits.empty()); // The parent is "Photos", not "Archive".
    }

    // Nothing to search for finds nothing.
    TEST(FileSearch, EmptyQueries) {
        const SearchVolume v = searchVolume();
        for (const std::string_view query : {"", "   ", "\\", "//"}) {
            const SearchResult result = find(v, query);
            EXPECT_TRUE(result.hits.empty()) << query;
            EXPECT_EQ(result.total, 0U) << query;
        }
    }

    // The limit cuts the hits after the best ranks; the total still counts every match.
    TEST(FileSearch, Limit) {
        const SearchVolume v = searchVolume();
        const SearchResult two = find(v, "photos", 2);
        EXPECT_EQ(filesOf(two), (std::vector{v.photos, v.archivePhotos}));
        EXPECT_EQ(two.total, 4U);
        const SearchResult none = find(v, "photos", 0);
        EXPECT_TRUE(none.hits.empty());
        EXPECT_EQ(none.total, 4U);
    }

    // ASCII lowercase of a string.
    std::string lower(std::string text) {
        std::ranges::transform(text, text.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; });
        return text;
    }

    // The matches of a query found the slow way: build every path, lowercase it and compare strings.
    std::vector<FileIndex> bruteForce(const Snapshot& snapshot, const std::string& query) {
        std::vector<std::pair<SearchRank, FileIndex>> found;
        const auto split = query.rfind('\\');
        const std::string name = split == std::string::npos ? query : query.substr(split + 1);
        const std::string directory = split == std::string::npos ? std::string() : query.substr(0, split);
        for (FileIndex file = 0; file < snapshot.files.size(); ++file) {
            const std::string path = lower(snapshot.path(file));
            const auto last = path.rfind('\\');
            const std::string own = path.substr(last + 1);
            const std::string parent = last == 0 ? std::string("\\") : path.substr(0, last);
            const bool named = split == std::string::npos ? own.contains(name) : own.starts_with(name);
            if (path == "\\" || !named || (split != std::string::npos && !parent.ends_with(directory))) {
                continue;
            }
            const SearchRank rank = own == name ? SearchRank::Exact
                : own.starts_with(name)         ? SearchRank::Prefix
                                                : SearchRank::Contains;
            found.emplace_back(rank, file);
        }
        std::ranges::stable_sort(found, {}, &std::pair<SearchRank, FileIndex>::first);
        std::vector<FileIndex> files;
        for (const auto& [rank, file] : found) {
            files.push_back(file);
        }
        return files;
    }

    // On a synthetic volume, name and path queries find what building every path finds, in the same order, and a
    // thread pool gives the same result as one thread.
    TEST(FileSearch, SyntheticVolumeMatchesBruteForce) {
        SynthOptions options;
        options.seed = 5;
        options.files = 4000;
        const Snapshot snapshot = generateSyntheticVolume(options);
        InlineExecutor serial;
        services::ThreadPool pool(4);
        // Synthetic names are "dir<n>" and "f<n><extension>".
        for (const std::string query : {"f12", "dir1", "f1.jpg", "dir3\\f", "r1\\dir2", "\\dir", "f9", ".mp4"}) {
            const std::vector<FileIndex> expected = bruteForce(snapshot, query);
            const SearchResult one = searchFiles(snapshot, query, 1'000'000, serial);
            EXPECT_EQ(filesOf(one), expected) << query;
            EXPECT_EQ(one.total, expected.size()) << query;
            const SearchResult many = searchFiles(snapshot, query, 1'000'000, pool);
            EXPECT_EQ(filesOf(many), expected) << query;
            const SearchResult cut = searchFiles(snapshot, query, 7, pool);
            const std::size_t kept = std::min<std::size_t>(7, expected.size());
            EXPECT_EQ(filesOf(cut), std::vector(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(kept)))
                << query;
        }
    }

}} // namespace icd::core
