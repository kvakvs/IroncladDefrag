// USN churn statistics (T073) from hand-made journal records: sessions counted from close records, directory churn
// from deleted children, flags spreading below churning directories. The volume check (dev scenario) is
// tests/integration/churn/check-churn.ps1.

#include "core/snapshot/SnapshotBuilder.h"
#include "services/activity/ChurnStats.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    // USN reason bits used below.
    constexpr std::uint32_t create = 0x100;
    constexpr std::uint32_t remove = 0x200;
    constexpr std::uint32_t extend = 0x2;
    constexpr std::uint32_t close = 0x8000'0000U;

    // A journal record.
    platform::UsnChange change(std::uint64_t file, std::uint64_t parent, std::uint32_t reason) {
        return {.fileReference = file, .parentReference = parent, .usn = 0, .reason = reason};
    }

    TEST(ChurnStats, CountsSessionsFromCloseRecords) {
        ChurnStats stats;
        const std::vector<platform::UsnChange> changes{
            change(10, 2, create),
            change(10, 2, create | extend),
            change(10, 2, create | extend | close),
            change(10, 2, extend),
            change(10, 2, extend | close), // A later append.
            change(11, 2, create | remove | close), // A temp file created and deleted in one session.
            change(12, 3, remove | close),
        };
        stats.add(changes);
        EXPECT_EQ(stats.records, changes.size());
        EXPECT_EQ(stats.sessions, 4U);
        EXPECT_EQ(stats.files[10].creates, 1U);
        EXPECT_EQ(stats.files[10].overwrites, 1U);
        EXPECT_EQ(stats.files[11].creates, 1U);
        EXPECT_EQ(stats.files[11].deletes, 1U);
        EXPECT_EQ(stats.files[11].overwrites, 0U);
        EXPECT_EQ(stats.directories[2].creates, 2U);
        EXPECT_EQ(stats.directories[2].deletes, 1U);
        EXPECT_EQ(stats.directories[3].deletes, 1U);
    }

    TEST(ChurnStats, DeletesCountWhereTheFileLived) {
        constexpr std::uint32_t renameOld = 0x1000;
        constexpr std::uint32_t renameNew = 0x2000;
        ChurnStats stats;
        stats.add(std::vector<platform::UsnChange>{
            change(10, 2, create | close),
            // A POSIX delete with another handle open: renamed into $Extend\$Deleted (99), then deleted there.
            change(10, 2, renameOld),
            change(10, 99, renameNew),
            change(10, 99, renameOld | renameNew | remove | close),
            change(11, 99, remove | close), // Never seen before the delete: stays where the record says.
        });
        EXPECT_EQ(stats.directories[2].creates, 1U);
        EXPECT_EQ(stats.directories[2].deletes, 1U);
        EXPECT_EQ(stats.directories[99].deletes, 1U);
    }

    TEST(ChurnStats, DeletesCountBeforeTheirCloseRecord) {
        ChurnStats stats;
        // Deleted while a scanner still holds the file: the close record comes later (here: not yet), once.
        stats.add(std::vector<platform::UsnChange>{change(10, 2, create | close), change(10, 2, remove)});
        EXPECT_EQ(stats.directories[2].deletes, 1U);
        stats.add(std::vector<platform::UsnChange>{change(10, 2, remove | close)});
        EXPECT_EQ(stats.directories[2].deletes, 1U); // Not twice.
        EXPECT_EQ(stats.files[10].deletes, 1U);
    }

    TEST(ChurnStats, DirectoriesChurnWhenManyCreatedFilesGoAgain) {
        const ChurnOptions options;
        EXPECT_TRUE(directoryChurns({.creates = 75, .deletes = 50, .overwrites = 0}, options)); // Build cycles.
        EXPECT_FALSE(directoryChurns({.creates = 300, .deletes = 0, .overwrites = 0}, options)); // Sources written.
        EXPECT_FALSE(
            directoryChurns({.creates = 200, .deletes = 20, .overwrites = 0}, options)); // A few culled photos.
        EXPECT_FALSE(directoryChurns({.creates = 9, .deletes = 9, .overwrites = 0}, options)); // Too few.
        EXPECT_TRUE(directoryChurns({.creates = 0, .deletes = 10, .overwrites = 0}, options)); // A cleanup.
    }

    // root (ref 1) / src (2) / a.cpp (3); root / build (4) / obj (5) / x.obj (6); root / log.txt (7).
    core::Snapshot tree() {
        core::SnapshotBuilder b({4096, 100});
        const auto root = b.addFile({"", core::noFile, 0, core::FileDirectory, {}, 1});
        const auto src = b.addFile({"src", root, 0, core::FileDirectory, {}, 2});
        b.addFile({"a.cpp", src, 1, 0, {}, 3});
        const auto build = b.addFile({"build", root, 0, core::FileDirectory, {}, 4});
        const auto obj = b.addFile({"obj", build, 0, core::FileDirectory, {}, 5});
        b.addFile({"x.obj", obj, 1, 0, {}, 6});
        b.addFile({"log.txt", root, 1, 0, {}, 7});
        return std::move(b).build();
    }

    TEST(ChurnStats, FlagsSpreadBelowChurningDirectories) {
        const core::Snapshot s = tree();
        ChurnStats stats;
        std::vector<platform::UsnChange> changes;
        for (std::uint64_t n = 0; n < 30; ++n) {
            changes.push_back(change(100 + n, 5, create | close)); // Objects come...
            changes.push_back(change(100 + n, 5, remove | close)); // ...and go.
            changes.push_back(change(200 + n, 2, create | close)); // Sources only come.
        }
        for (int n = 0; n < 12; ++n) {
            changes.push_back(change(7, 1, extend | close)); // The log is appended to all the time.
        }
        stats.add(changes);
        core::InlineExecutor inline_;
        const auto flags = churnFlags(s, stats, {}, inline_);
        const std::vector<std::uint8_t> expected{0, 0, 0, 0, 1, 1, 1}; // obj, x.obj below it, and the log.
        EXPECT_EQ(flags, expected);
        ThreadPool pool(3);
        EXPECT_EQ(churnFlags(s, stats, {}, pool), expected);
        EXPECT_EQ(churnDirectories(s, stats, {}), std::vector<std::string>{"\\build\\obj"});
    }

    // obj (ref 1) holding d0, d1, d2 (refs 10-12) with many objects rebuilt and a small d3 (13) with a few, plus src
    // (14) with a single deleted file.
    TEST(ChurnStats, SmallFoldersChurnWithTheirSiblings) {
        core::SnapshotBuilder b({4096, 100});
        const auto root = b.addFile({"", core::noFile, 0, core::FileDirectory, {}, 1});
        for (std::uint64_t d = 0; d < 4; ++d) {
            b.addFile({"d" + std::to_string(d), root, 0, core::FileDirectory, {}, 10 + d});
        }
        b.addFile({"src", root, 0, core::FileDirectory, {}, 14});
        const core::Snapshot s = std::move(b).build();
        std::vector<platform::UsnChange> changes;
        std::uint64_t file = 100;
        const auto rebuild = [&](std::uint64_t dir, int files) {
            for (int n = 0; n < files; ++n, ++file) {
                changes.push_back(change(file, dir, create | close));
                changes.push_back(change(file, dir, remove | close));
                changes.push_back(change(file + 10'000, dir, create | close));
            }
        };
        for (std::uint64_t d = 10; d < 13; ++d) {
            rebuild(d, 20);
        }
        rebuild(13, 3); // Three deletes: too few alone, but its siblings churn.
        changes.push_back(change(5000, 14, create | close));
        changes.push_back(change(5000, 14, remove | close));
        ChurnStats stats;
        stats.add(changes);
        const std::vector<std::string> expected{"\\d0", "\\d1", "\\d2", "\\d3"}; // Not src: its siblings are dN.
        EXPECT_EQ(churnDirectories(s, stats, {}), expected);
    }

}} // namespace icd::services
