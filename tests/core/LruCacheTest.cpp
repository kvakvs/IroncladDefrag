#include "core/map/MapData.h"
#include "core/synth/SyntheticVolume.h"
#include "core/util/LruCache.h"

#include <gtest/gtest.h>

#include <string>

namespace icd::core { namespace {

    // Entries beyond the cost limit push out the least recently used ones.
    TEST(LruCache, EvictsLeastRecentlyUsed) {
        LruCache<int, std::string> cache(30);
        cache.insert(1, "a", 10);
        cache.insert(2, "b", 10);
        cache.insert(3, "c", 10);
        ASSERT_NE(cache.find(1), nullptr); // 1 is now the most recent.
        cache.insert(4, "d", 10); // Over the limit: 2 is the oldest.
        EXPECT_FALSE(cache.contains(2));
        EXPECT_TRUE(cache.contains(1));
        EXPECT_TRUE(cache.contains(3));
        EXPECT_EQ(*cache.find(4), "d");
        EXPECT_EQ(cache.cost(), 30u);
    }

    // Replacing an entry updates its value and cost; an entry larger than the limit stays alone.
    TEST(LruCache, ReplaceEraseAndOversize) {
        LruCache<int, std::string> cache(30);
        cache.insert(1, "a", 10);
        cache.insert(1, "A", 20);
        EXPECT_EQ(cache.size(), 1u);
        EXPECT_EQ(cache.cost(), 20u);
        EXPECT_EQ(*cache.find(1), "A");
        cache.insert(2, "huge", 100);
        EXPECT_EQ(cache.size(), 1u); // Everything else was evicted, the new entry stays.
        EXPECT_TRUE(cache.contains(2));
        cache.erase(2);
        cache.erase(7); // Unknown keys are ignored.
        EXPECT_EQ(cache.size(), 0u);
        EXPECT_EQ(cache.cost(), 0u);
        cache.insert(3, "c", 5);
        cache.clear();
        EXPECT_EQ(cache.find(3), nullptr);
    }

    // Tile ids work as cache keys.
    TEST(LruCache, TileKeys) {
        LruCache<TileId, int, TileIdHash> cache(2);
        cache.insert({0, 1, 2}, 7, 1);
        cache.insert({-3, 1, 2}, 8, 1);
        EXPECT_EQ(*cache.find({0, 1, 2}), 7);
        EXPECT_EQ(*cache.find({-3, 1, 2}), 8);
        EXPECT_EQ(cache.find({0, 2, 1}), nullptr);
    }

    // Map data bundles a world for the aspect, a pyramid and flags, and renders tiles through its source.
    TEST(MapData, BuildsEverythingTheMapNeeds) {
        SynthOptions options;
        options.files = 2000;
        options.geometry.totalClusters = 1ULL << 20U;
        InlineExecutor serial;
        const auto map = MapData::build(generateSyntheticVolume(options), {.width = 16, .height = 9}, serial);
        EXPECT_EQ(map->world().clusters(), 1ULL << 20U);
        EXPECT_GT(map->world().rowWidth(), map->world().rows()); // Landscape.
        EXPECT_EQ(map->pyramid().usedClusters({map->pyramid().topLevel(), 0, 0}),
                  map->snapshot().geometry.totalClusters - map->snapshot().freeSpace.freeClusters());
        EXPECT_GT(map->memoryBytes(), map->snapshot().extents.size()); // Flags plus pyramid.
        const auto pixels = renderTile(map->source(), {map->world().maxLevel(), 0, 0}, OccupancyPalette{});
        EXPECT_EQ(pixels.size(), tilePixels);
    }

}} // namespace icd::core
