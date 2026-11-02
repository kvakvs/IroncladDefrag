// Sorted zones (T086): the byte-weighted LIS against brute force, sort keys per order, the sort bound with
// tolerance, and destinations that leave the zone in order.

#include "core/planner/CandidateFilter.h"
#include "core/planner/SortZone.h"
#include "core/planner/Validator.h"
#include "core/synth/SynthRandom.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <map>
#include <set>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::Effort;
    using strategy::ZoneAction;
    using strategy::ZoneOrder;

    TEST(SortZone, WeightedLisEqualsBruteForce) {
        SynthRandom random(21);
        for (int round = 0; round < 400; ++round) {
            const std::size_t n = random.below(11);
            std::vector<std::uint32_t> ranks(n);
            std::vector<ClusterCount> weights(n);
            for (std::size_t i = 0; i < n; ++i) {
                ranks[i] = static_cast<std::uint32_t>(i);
                weights[i] = 1 + random.below(9);
            }
            for (std::size_t i = n; i > 1; --i) {
                std::swap(ranks[i - 1], ranks[random.below(i)]);
            }
            ClusterCount best = 0;
            for (std::uint32_t mask = 0; mask < (1U << n); ++mask) {
                ClusterCount sum = 0;
                std::int64_t last = -1;
                bool increasing = true;
                for (std::size_t i = 0; i < n && increasing; ++i) {
                    if ((mask >> i & 1U) != 0) {
                        increasing = static_cast<std::int64_t>(ranks[i]) > last;
                        last = ranks[i];
                        sum += weights[i];
                    }
                }
                best = increasing ? std::max(best, sum) : best;
            }
            const LisResult lis = weightedLis(ranks, weights);
            EXPECT_EQ(lis.weight, best) << round;
            ClusterCount kept = 0;
            std::int64_t last = -1;
            for (std::size_t i = 0; i < n; ++i) {
                if (lis.keep[i] != 0) {
                    EXPECT_GT(static_cast<std::int64_t>(ranks[i]), last); // The kept items are in order.
                    last = ranks[i];
                    kept += weights[i];
                }
            }
            EXPECT_EQ(kept, best);
        }
    }

    TEST(SortZone, KeysFollowEachOrder) {
        SnapshotBuilder b({.bytesPerCluster = 4096, .totalClusters = 100});
        const auto add = [&](std::string_view name, FileIndex parent, std::uint16_t flags, std::int64_t modified,
                             std::uint64_t size) {
            return b.addFile({.name = name,
                              .parent = parent,
                              .sizeBytes = size,
                              .flags = flags,
                              .times = {.created = modified, .modified = modified, .accessed = modified},
                              .fileReference = 0});
        };
        const FileIndex root = add("", noFile, FileDirectory, 0, 0);
        const FileIndex beta = add("beta", root, FileDirectory, 0, 0);
        const FileIndex alpha = add("alpha", root, FileDirectory, 0, 0);
        const FileIndex x = add("x.txt", beta, 0, 30, 10);
        const FileIndex y = add("y.txt", alpha, 0, 10, 30);
        const FileIndex big = add("B.txt", alpha, 0, 20, 20);
        const FileIndex a = add("a.txt", alpha, 0, 40, 5);
        b.addFreeRun({0, 100});
        const Snapshot s = std::move(b).build();
        const SortKeys dir(s, ZoneOrder::DirectoryName);
        EXPECT_TRUE(dir.before(a, big)); // Case-insensitive names: a < B < y.
        EXPECT_TRUE(dir.before(big, y));
        EXPECT_TRUE(dir.before(y, x)); // Everything in alpha before beta.
        EXPECT_TRUE(dir.before(alpha, beta));
        const SortKeys name(s, ZoneOrder::Name);
        EXPECT_TRUE(name.before(y, x) == false);
        EXPECT_TRUE(name.before(x, y));
        const SortKeys size(s, ZoneOrder::Size);
        EXPECT_TRUE(size.before(a, x)); // Smallest first.
        EXPECT_TRUE(size.before(big, y));
        const SortKeys age(s, ZoneOrder::Age);
        EXPECT_TRUE(age.before(a, x)); // Newest first, oldest at the end.
        EXPECT_TRUE(age.before(big, y));
        const SortKeys none(s, ZoneOrder::None);
        EXPECT_TRUE(none.before(x, y)); // Ties go by file index.
    }

    // Letters are documents named by the map, one per cluster.
    test::PictureFacts named(std::map<char, std::string> names) {
        test::PictureFacts facts;
        for (const auto& [letter, name] : names) {
            facts.classes[letter] = "source";
        }
        facts.names = std::move(names);
        return facts;
    }

    // One sort zone of source files by name.
    strategy::ZoneScript byName() {
        strategy::Zone z = test::zone("Docs", {"source"}, ZoneAction::Sort, Anchor::Front);
        z.order = ZoneOrder::Name;
        return test::script({z});
    }

    // Plans a sorted zone end to end: bound, sort bound, candidates, destinations.
    struct SortCase {
        ZoneAssignment assignment;
        TargetLayout layout;
        LowerBound bound;
        std::vector<SortedZone> sorted;
        Candidates candidates;
        FillResult result;
    };

    // Runs the sorted-zone planning on a snapshot.
    SortCase plan(const Snapshot& snapshot, std::span<const classify::ClassId> classes, const strategy::ZoneScript& s,
                  double tolerance) {
        InlineExecutor serial;
        SortCase c;
        c.assignment = test::assignSnapshot(snapshot, classes, s);
        c.layout = resolveLayout(snapshot, s, c.assignment, classes, classify::builtinCatalog(), {});
        c.bound = lowerBound(snapshot, s, c.assignment, c.layout, {}, serial);
        c.sorted = applySortBounds(snapshot, s, c.assignment, Effort::Maximum, tolerance, c.bound);
        c.candidates = filterCandidates(c.bound, c.assignment, s.zones.size());
        HoleSet pool(snapshot.freeSpace.runs());
        for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
            if (c.bound.mustMove[f] != 0) {
                releaseRuns(pool, movableRuns(snapshot, static_cast<FileIndex>(f)));
            }
        }
        const ZoneTarget& t = c.layout.zones.front();
        const FillOptions options{.zone = t.zone, .home = {0, t.endLcn()}};
        c.result = sortZone(snapshot, t, c.sorted.front(), c.candidates.perZone[t.zone], pool, options);
        return c;
    }

    TEST(SortZone, MoversGoBetweenTheirAnchorsAndTheZoneEndsSorted) {
        //  0 1 2 3 4 5 6    names: P=b Q=a R=c S=d; zone [0,4)
        //  . P Q R S . .    P and R stay (heaviest in order); Q goes before P, R gives way for S.
        const auto v = test::pictureVolume(".PQRS..", named({{'P', "b"}, {'Q', "a"}, {'R', "c"}, {'S', "d"}}));
        const SortCase c = plan(v.snapshot, v.classes, byName(), 1.0);
        EXPECT_EQ(c.bound.clusters, 2U); // Q is out of order, S outside the zone.
        ASSERT_TRUE(c.sorted.front().sorted);
        EXPECT_EQ(c.sorted.front().anchors, (std::vector<FileIndex>{v.files.at('P'), v.files.at('R')}));
        ASSERT_EQ(c.result.placed.size(), 3U);
        EXPECT_TRUE(c.result.overflow.empty());
        std::map<FileIndex, Lcn> at;
        for (const Destination& d : c.result.placed) {
            at[d.file] = d.runs.front().start;
        }
        EXPECT_EQ(at[v.files.at('Q')], 0U);
        EXPECT_EQ(at[v.files.at('R')], 2U);
        EXPECT_EQ(at[v.files.at('S')], 3U);
        Plan p;
        p.lowerBound = c.bound.clusters;
        appendMoves(v.snapshot, c.result.placed, p);
        for (const Move& m : p.moves) {
            p.extra += m.extra ? m.clusters : 0;
        }
        EXPECT_EQ(p.extra, 1U); // R.
        finishTotals(p, v.snapshot);
        const Validation result = validatePlan(v.snapshot, p, {.layout = &c.layout});
        EXPECT_TRUE(result.ok()) << result.issues.front().message;
    }

    TEST(SortZone, ToleranceLeavesFarFromSortedZonesUnsorted) {
        // In reverse order: the heaviest sorted subset is one file, so sorting would move three quarters.
        const auto v = test::pictureVolume("DCBA..", named({{'A', "a"}, {'B', "b"}, {'C', "c"}, {'D', "d"}}));
        const SortCase loose = plan(v.snapshot, v.classes, byName(), 0.5);
        EXPECT_FALSE(loose.sorted.front().sorted);
        EXPECT_EQ(loose.bound.clusters, 0U); // Unsorted zones keep the fast-fill bound: all four are in place.
        const SortCase strict = plan(v.snapshot, v.classes, byName(), 1.0);
        EXPECT_TRUE(strict.sorted.front().sorted);
        EXPECT_EQ(strict.bound.clusters, 3U);
        EXPECT_FALSE(sortsAt(byName().zones.front(), Effort::Fast)); // Never at Fast.
        EXPECT_TRUE(sortsAt(byName().zones.front(), Effort::Moderate));
    }

    TEST(SortZone, SyntheticZoneEndsInOrder) {
        SynthOptions options;
        options.seed = 31;
        options.files = 1500;
        options.geometry.totalClusters = 1ULL << 19U;
        options.fillPercent = 40;
        options.fragmentedShare = 0.1;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const auto classes = test::classesOf(snapshot);
        strategy::Zone all = test::zone("All", {}, ZoneAction::Sort, Anchor::Front);
        all.order = ZoneOrder::Size;
        const auto s = test::script({all});
        const SortCase c = plan(snapshot, classes, s, 1.0);
        // Final position of every file in the zone: anchors stay unless given up, placed files move; overflow is left
        // out.
        std::map<FileIndex, Lcn> at;
        for (const FileIndex f : c.sorted.front().anchors) {
            at[f] = sortedRuns(movableRuns(snapshot, f)).front().start;
        }
        std::set<FileIndex> released(c.result.released.begin(), c.result.released.end());
        for (const FileIndex f : released) {
            at.erase(f);
        }
        for (const Destination& d : c.result.placed) {
            at[d.file] = d.runs.front().start;
        }
        std::vector<std::pair<Lcn, FileIndex>> finals;
        for (const auto& [file, lcn] : at) {
            finals.emplace_back(lcn, file);
        }
        std::ranges::sort(finals);
        const SortKeys& keys = *c.sorted.front().keys;
        for (std::size_t i = 1; i < finals.size(); ++i) {
            ASSERT_TRUE(keys.before(finals[i - 1].second, finals[i].second)) << i;
        }
        EXPECT_GT(c.result.placed.size(), 100U);
        EXPECT_LT(c.result.overflow.size(), c.result.placed.size() / 4);
    }

}} // namespace icd::core::planner
