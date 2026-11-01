// Snapshot persistence (T064): save -> load gives an identical snapshot and meta (any block size, any executor, with
// the real XPRESS codec), and damaged, truncated, foreign or other-version files are rejected cleanly, never half
// loaded.

#include "core/snapshot/SnapshotIO.h"
#include "core/synth/SyntheticVolume.h"
#include "platform/win/Compression.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/ScanCompare.h"
#include "services/snapshot/SnapshotStore.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace icd::services { namespace {

    using core::SnapshotFormatError;

    // Stores blocks as they are (codec id 7, never smaller, so every block falls back to "stored").
    class IdentityCodec : public core::BlockCodec {
    public:
        [[nodiscard]] std::uint32_t id() const override { return 7; }
        [[nodiscard]] std::vector<std::byte> compress(std::span<const std::byte> raw) const override {
            return {raw.begin(), raw.end()};
        }
        [[nodiscard]] std::vector<std::byte> decompress(std::span<const std::byte> stored,
                                                        std::size_t /*rawSize*/) const override {
            return {stored.begin(), stored.end()};
        }
    };

    // A synthetic volume with a USN cursor.
    core::Snapshot sample() {
        core::SynthOptions options;
        options.files = 3000;
        core::Snapshot s = core::generateSyntheticVolume(options);
        s.usn = core::UsnCursor{0x01D9'1234'5678'9ABCULL, 987654321};
        return s;
    }

    // Meta of the sample.
    core::SnapshotMeta sampleMeta(const core::Snapshot& s) {
        return {.volumeName = "\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\",
                .volumeSerial = 0x1122'3344'5566'7788ULL,
                .scanTime = 133'000'000'000'000'000,
                .aspect = {2560, 1440},
                .scanner = 1,
                .files = s.files.size(),
                .extents = s.extents.size()};
    }

    // The bytes of a saved snapshot.
    std::vector<std::byte> saved(const core::Snapshot& s, const core::BlockCodec& codec, core::Executor& executor,
                                 core::SnapshotFileOptions options = {}) {
        core::MemorySink sink;
        core::writeSnapshotFile(s, sampleMeta(s), sink, codec, executor, options);
        return sink.bytes;
    }

    // Loads bytes.
    core::LoadedSnapshot loaded(const std::vector<std::byte>& bytes, const core::BlockCodec& codec,
                                core::Executor& executor, core::SnapshotFileOptions options = {}) {
        core::MemorySource source(bytes);
        return core::readSnapshotFile(source, codec, executor, options);
    }

    TEST(SnapshotIO, RoundTripIsIdentical) {
        const core::Snapshot s = sample();
        core::InlineExecutor serial;
        const platform::XpressCodec xpress;
        const auto bytes = saved(s, xpress, serial);
        const core::LoadedSnapshot back = loaded(bytes, xpress, serial);
        EXPECT_EQ(back.meta, sampleMeta(s));
        const ScanComparison c = compareExact(s, back.snapshot);
        EXPECT_TRUE(c.same()) << (c.samples.empty() ? "" : c.samples.front());
        EXPECT_EQ(back.snapshot.usn->journalId, s.usn->journalId);
        EXPECT_EQ(back.snapshot.usn->nextUsn, s.usn->nextUsn);
        EXPECT_EQ(back.snapshot.path(100), s.path(100)); // The rebuilt directory tree.
        EXPECT_EQ(back.snapshot.freeSpace.freeClusters(), s.freeSpace.freeClusters());
        std::size_t raw = 0;
        {
            core::MemorySink plain;
            core::writeSnapshotFile(s, sampleMeta(s), plain, IdentityCodec(), serial);
            raw = plain.bytes.size();
        }
        EXPECT_LT(bytes.size(), raw); // XPRESS compresses.
    }

    TEST(SnapshotIO, SameBytesForAnyExecutorAndBlockSize) {
        const core::Snapshot s = sample();
        core::InlineExecutor serial;
        ThreadPool pool(4);
        const platform::XpressCodec xpress;
        const core::SnapshotFileOptions small{.blockBytes = 4096, .batchBlocks = 3};
        const auto one = saved(s, xpress, serial, small);
        EXPECT_EQ(one, saved(s, xpress, pool, small));
        const ScanComparison c = compareExact(s, loaded(one, xpress, pool, small).snapshot);
        EXPECT_TRUE(c.same());
    }

    TEST(SnapshotIO, MetaReadsWithoutTheData) {
        const core::Snapshot s = sample();
        core::InlineExecutor serial;
        const auto bytes = saved(s, IdentityCodec(), serial);
        core::MemorySource source(bytes);
        EXPECT_EQ(core::readSnapshotMeta(source), sampleMeta(s));
    }

    TEST(SnapshotIO, RejectsOtherFilesAndVersions) {
        const core::Snapshot s = sample();
        core::InlineExecutor serial;
        const IdentityCodec codec;
        auto bytes = saved(s, codec, serial);
        auto version = bytes;
        version[8] = std::byte{2}; // Format version 2 (or any other) is not read.
        EXPECT_THROW(loaded(version, codec, serial), SnapshotFormatError);
        try {
            (void)loaded(version, codec, serial);
        }
        catch (const SnapshotFormatError& e) {
            EXPECT_NE(std::string(e.what()).find("version"), std::string::npos);
        }
        std::vector<std::byte> garbage(1000, std::byte{0x41});
        EXPECT_THROW(loaded(garbage, codec, serial), SnapshotFormatError);
        EXPECT_THROW(loaded({}, codec, serial), SnapshotFormatError);
    }

    // Every single damaged byte is caught (checksums cover the header and each block), and so is any truncation.
    TEST(SnapshotIO, DamageAndTruncationAreRejected) {
        core::SynthOptions options;
        options.files = 200;
        const core::Snapshot s = core::generateSyntheticVolume(options);
        core::InlineExecutor serial;
        const platform::XpressCodec codec;
        const core::SnapshotFileOptions small{.blockBytes = 2048, .batchBlocks = 2};
        const auto bytes = saved(s, codec, serial, small);
        std::mt19937 random(5);
        for (int round = 0; round < 400; ++round) {
            auto damaged = bytes;
            const std::size_t at = random() % damaged.size();
            damaged[at] ^= static_cast<std::byte>(1 + random() % 255);
            EXPECT_THROW(loaded(damaged, codec, serial, small), SnapshotFormatError) << "byte " << at;
        }
        for (std::size_t at = 0; at < 160; ++at) { // Every byte of the header, meta and first block header.
            auto damaged = bytes;
            damaged[at] ^= std::byte{0x10};
            EXPECT_THROW(loaded(damaged, codec, serial, small), SnapshotFormatError) << "header byte " << at;
        }
        for (std::size_t size = 0; size < bytes.size(); size += 1 + bytes.size() / 300) {
            const std::vector<std::byte> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
            EXPECT_THROW(loaded(cut, codec, serial, small), SnapshotFormatError) << "size " << size;
        }
    }

    TEST(SnapshotStore, SavesLoadsAndRejectsDamagedFiles) {
        const auto dir = std::filesystem::temp_directory_path() / "icd-snapshot-store-test";
        std::filesystem::remove_all(dir);
        const SnapshotStore store(dir);
        const core::Snapshot s = sample();
        const core::SnapshotMeta meta = sampleMeta(s);
        ThreadPool pool(4);
        std::string why;
        EXPECT_FALSE(store.load(meta.volumeName, pool, &why));
        EXPECT_EQ(why, "no saved snapshot");
        store.save(s, meta, pool);
        EXPECT_EQ(store.pathFor(meta.volumeName).filename(), "Volume{01234567-89ab-cdef-0123-456789abcdef}.icds");
        EXPECT_EQ(store.peek(meta.volumeName), meta);
        const auto back = store.load(meta.volumeName, pool, &why);
        ASSERT_TRUE(back) << why;
        EXPECT_TRUE(compareExact(s, back->snapshot).same());

        {
            std::fstream file(store.pathFor(meta.volumeName), std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(static_cast<std::streamoff>(std::filesystem::file_size(store.pathFor(meta.volumeName)) / 2));
            file.put('\x5A');
        }
        EXPECT_FALSE(store.load(meta.volumeName, pool, &why));
        EXPECT_FALSE(why.empty());
        store.save(s, meta, pool); // Replaces the damaged file.
        EXPECT_TRUE(store.load(meta.volumeName, pool));
        store.remove(meta.volumeName);
        EXPECT_FALSE(store.peek(meta.volumeName));
        std::filesystem::remove_all(dir);
    }

}} // namespace icd::services
