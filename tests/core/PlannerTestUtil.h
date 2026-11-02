#pragma once

// Test helpers for the planner: tiny volumes drawn as pictures, one character per cluster, and zone scripts.
//   '.'  free cluster
//   '#'  unmovable metadata ($MFT-like, its own file)
//   any other character: a cluster of the file with that name; repeated characters with gaps make fragments.

#include "core/classify/Catalog.h"
#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/strategy/ZoneScript.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace icd::core::planner::test {

    // FILETIME ticks per day.
    inline constexpr std::int64_t day = 864'000'000'000;

    // The "now" of planner tests.
    inline constexpr std::int64_t now = 20'000 * day;

    // What a picture does not say about its files.
    struct PictureFacts {
        std::map<char, std::string> classes{}; // Catalog class id per file letter; others are "unknown".
        std::map<char, std::int64_t> modified{}; // Modification time per letter; others now - 100 days.
        std::map<char, std::string> names{}; // File name per letter; others the letter itself.
        std::map<char, std::uint16_t> flags{}; // Extra FileFlag bits per letter.
        std::uint32_t clusterBytes = 4096; // Cluster size.
    };

    // A snapshot drawn from a picture, with its classes.
    struct PictureVolume {
        Snapshot snapshot; // The volume.
        std::vector<classify::ClassId> classes{}; // Per file.
        std::map<char, FileIndex> files{}; // Letter to file index.
    };

    // Builds the volume a picture shows. File runs are numbered in LCN order (their VCN order).
    inline PictureVolume pictureVolume(std::string_view picture, const PictureFacts& facts = {}) {
        const classify::Catalog& catalog = classify::builtinCatalog();
        SnapshotBuilder builder({.bytesPerCluster = facts.clusterBytes, .totalClusters = picture.size()});
        PictureVolume v;
        const FileIndex root = builder.addFile(
            {.name = "", .parent = noFile, .sizeBytes = 0, .flags = FileDirectory, .times = {}, .fileReference = 0});
        v.classes.push_back(*catalog.find("directories"));
        std::map<char, std::uint64_t> clusters;
        for (const char c : picture) {
            ++clusters[c];
        }
        for (const auto& [letter, count] : clusters) {
            if (letter == '.') {
                continue;
            }
            const bool meta = letter == '#';
            const auto name = facts.names.contains(letter) ? facts.names.at(letter) : std::string(1, letter);
            const std::int64_t modified = facts.modified.contains(letter) ? facts.modified.at(letter) : now - 100 * day;
            const std::uint16_t extra = facts.flags.contains(letter) ? facts.flags.at(letter) : 0;
            const std::string fileName = meta ? std::string("$MFT") : name;
            const FileEntry entry{.name = fileName,
                                  .parent = root,
                                  .sizeBytes = count * facts.clusterBytes,
                                  .flags =
                                      static_cast<std::uint16_t>((meta ? FileMetadata | FileUnmovable : 0) | extra),
                                  .times = {.created = modified, .modified = modified, .accessed = modified},
                                  .fileReference = 0};
            v.files[letter] = builder.addFile(entry);
            const std::string cls =
                meta ? "metadata" : (facts.classes.contains(letter) ? facts.classes.at(letter) : "unknown");
            v.classes.push_back(*catalog.find(cls));
        }
        std::map<char, std::uint64_t> vcn;
        for (std::size_t i = 0; i < picture.size();) {
            const char c = picture[i];
            std::size_t j = i;
            while (j < picture.size() && picture[j] == c) {
                ++j;
            }
            const ClusterRun run{i, j - i};
            if (c == '.') {
                builder.addFreeRun(run);
            }
            else {
                builder.addExtent({.run = run,
                                   .vcn = vcn[c],
                                   .file = v.files[c],
                                   .stream = 0,
                                   .flags = static_cast<std::uint16_t>(c == '#' ? ExtentUnmovable : 0)});
                vcn[c] += run.length;
            }
            i = j;
        }
        v.snapshot = std::move(builder).build();
        return v;
    }

    // A zone selecting catalog classes.
    inline strategy::Zone zone(std::string name, std::vector<std::string> classes, strategy::ZoneAction action,
                               strategy::Anchor anchor = strategy::Anchor::AfterPrevious) {
        strategy::Zone z;
        z.name = std::move(name);
        z.select.classes = std::move(classes);
        z.action = action;
        z.anchor = anchor;
        return z;
    }

    // A script of zones.
    inline strategy::ZoneScript script(std::vector<strategy::Zone> zones) {
        return {.id = "test", .name = "Test", .description = "", .zones = std::move(zones)};
    }

    // Zones and roles of a picture volume under a script.
    inline ZoneAssignment assign(const PictureVolume& v, const strategy::ZoneScript& s) {
        InlineExecutor inline_;
        return assignZones(v.snapshot, s, classify::builtinCatalog(), {.classes = v.classes, .churn = {}, .now = now},
                           inline_);
    }

} // namespace icd::core::planner::test
