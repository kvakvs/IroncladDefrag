#pragma once

#include "core/map/Viewport.h"
#include "core/map/World.h"
#include "core/snapshot/Snapshot.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace icd::core {

    // Where a fragment sits among its file's fragments, counted from 1 as people read it.
    struct FragmentPosition {
        std::size_t index = 0; // Fragment holding the record, from 1 (0: not found).
        std::size_t count = 0; // Fragments of the file.
    };

    // The fragment of its file that holds the record at a position of the extent index. Split long runs that
    // continue on disk count as one fragment, as in ExtentIndex::fragmentCount().
    [[nodiscard]] FragmentPosition fragmentOf(const ExtentIndex& extents, std::size_t record);

    // The cells one window pixel covers and how much of them is in use; several cells at coarse zoom, so the
    // tooltip can say what a whole pixel stands for, not only the cluster under the pointer.
    struct PixelArea {
        CellRect cells; // Cells under the pixel, clipped to the world.
        ClusterCount clusters = 0; // Clusters of the volume among those cells.
        ClusterCount used = 0; // Of those, owned by extents.
    };

    // What the map shows under the pointer (plan.md 7.4 hover).
    struct MapHit {
        CellPoint cell; // Cell under the pointer.
        Lcn lcn = 0; // Its cluster.
        std::optional<std::size_t> record; // Position in the extent index of the extent holding lcn; none if free.
        FragmentPosition fragment; // With a record: fragment i of n of its file.
        std::optional<ClusterRun> freeRun; // Without a record: the free run holding lcn, if listed.
        PixelArea pixel; // Everything the pointer's pixel covers.
    };

    // Clusters owned by extents in a cell rectangle (clipped to the world and the last cluster).
    [[nodiscard]] ClusterCount usedClustersInRect(const Snapshot& snapshot, const World& world, CellRect rect);

    // The cells under the window pixel that contains a point, clipped to the world; empty outside it.
    [[nodiscard]] CellRect pixelCells(const World& world, const Viewport& view, PixelPoint point);

    // What lies under a pointer position, or nullopt off the volume (outside the world or past the last
    // cluster). Pure lookups: a binary search for the extent, one per covered row for the pixel's usage.
    [[nodiscard]] std::optional<MapHit> hitTest(const Snapshot& snapshot, const World& world, const Viewport& view,
                                                PixelPoint pointer);

    // A FILETIME (100 ns ticks since 1601, UTC) as "2026-10-31 14:22" in a time zone, or with " UTC" when zone
    // is null; "-" for an unset time.
    [[nodiscard]] std::string formatFileTime(std::int64_t fileTime, const std::chrono::time_zone* zone);

    // One labelled fact about a file ("Size", "1.2 GB"), for the File tab.
    struct FileFact {
        std::string label; // What it is.
        std::string value; // Its value.
    };

    // Facts about a file for the File tab (T127): size and allocation, fragments, type and flags, class (when per-file
    // classes are given), modified, created and accessed times in zone (null: UTC).
    [[nodiscard]] std::vector<FileFact> describeFile(const Snapshot& snapshot, FileIndex file,
                                                     const std::chrono::time_zone* zone,
                                                     std::span<const std::uint8_t> fileClasses = {});

    // The tooltip of a hit, one line per fact: cluster and byte offset, then the owning file (path, stream,
    // fragment i/n, size, type and flags, times) or the free run, then the pixel's coverage at coarse zoom.
    // Times are shown in zone (null: UTC). With per-record classes (MapFileFacts::extentKinds) a "Class:" line follows
    // the type.
    [[nodiscard]] std::vector<std::string> describeHit(const Snapshot& snapshot, const MapHit& hit,
                                                       const std::chrono::time_zone* zone,
                                                       std::span<const std::uint8_t> extentKinds = {});

} // namespace icd::core
