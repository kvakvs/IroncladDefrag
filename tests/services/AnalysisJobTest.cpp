// The analysis job (T048): the bitmap-only snapshot, a full run on the `special` test volume (maps arrive, the last
// one is the full snapshot, the summary matches it) and cancellation. The volume tests need administrator rights
// and a tools/testvol volume; they are skipped otherwise.

#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"
#include "services/drives/SystemDriveProvider.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/AnalysisJob.h"

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>

namespace {

    using namespace icd;

    // Collects progress stages and can cancel after a number of reports.
    class RecordingSink : public core::ProgressSink {
    public:
        // Cancels `source` once this many reports arrived (0: never).
        RecordingSink(core::CancelSource* source, std::size_t cancelAfter) :
            source_(source), cancelAfter_(cancelAfter) {}

        void report(const core::Progress& progress) override {
            const std::scoped_lock lock(mutex_);
            if (stages_.empty() || stages_.back() != progress.stage) {
                stages_.push_back(progress.stage);
            }
            if (cancelAfter_ != 0 && ++count_ == cancelAfter_ && source_ != nullptr) {
                source_->cancel();
            }
        }

        // Stages in the order they first appeared.
        std::vector<std::string> stages() {
            const std::scoped_lock lock(mutex_);
            return stages_;
        }

    private:
        std::mutex mutex_; // Guards the fields below.
        std::vector<std::string> stages_; // Distinct consecutive stages.
        core::CancelSource* source_; // What to cancel.
        std::size_t cancelAfter_; // Report count that triggers it.
        std::size_t count_ = 0; // Reports so far.
    };

    // The test volume with the special scenario as the drive provider lists it.
    std::optional<core::DriveInfo> specialDrive() {
        if (!platform::isElevated()) {
            return std::nullopt;
        }
        for (const auto& drive : services::SystemDriveProvider().listDrives()) {
            const std::wstring root = platform::fromUtf8(drive.root);
            if (drive.label.starts_with("ICD-TEST") &&
                GetFileAttributesW((root + L"special").c_str()) != INVALID_FILE_ATTRIBUTES) {
                return drive;
            }
        }
        return std::nullopt;
    }

    TEST(AnalysisJob, BitmapSnapshotOwnsAllUsedSpace) {
        const core::Snapshot snapshot = services::bitmapSnapshot({4096, 1000}, {{10, 90}, {500, 100}});
        EXPECT_EQ(snapshot.freeSpace.freeClusters(), 190U);
        core::ClusterCount used = 0;
        for (const auto& record : snapshot.extents.records()) {
            used += record.length();
            EXPECT_EQ(record.file(), 1U); // The placeholder.
            EXPECT_NE(record.flags() & core::ExtentUnmovable, 0);
        }
        EXPECT_EQ(used, 810U);
        EXPECT_EQ(snapshot.path(1), "\\(files not read yet)");
    }

    TEST(AnalysisJob, AnalysesTheSpecialVolume) {
        const auto drive = specialDrive();
        if (!drive) {
            GTEST_SKIP() << "needs administrator rights and an ICD-TEST volume with the special scenario";
        }
        services::ThreadPool pool(4);
        auto output = std::make_shared<services::AnalysisOutput>();
        RecordingSink sink(nullptr, 0);
        services::JobContext context{{}, sink, pool};
        auto job = services::makeAnalysisJob(*drive, {16, 9}, output);
        job(context);
        const auto map = output->take();
        ASSERT_NE(map, nullptr);
        const auto summary = output->takeSummary();
        ASSERT_TRUE(summary.has_value());
        ASSERT_TRUE(summary->scan.has_value());
        EXPECT_GT(map->snapshot().files.size(), 3000U); // The final map holds every file.
        EXPECT_GT(summary->fragmentedFiles, 0U); // many.bin and the compressed logs.
        EXPECT_EQ(summary->freeBytes,
                  map->snapshot().freeSpace.freeClusters() * map->snapshot().geometry.bytesPerCluster);
        const auto stages = sink.stages();
        ASSERT_GE(stages.size(), 3U);
        EXPECT_EQ(stages.front(), "Reading volume bitmap");
        EXPECT_NE(std::ranges::find(stages, "Reading files"), stages.end());
        EXPECT_EQ(stages.back(), "Building map");
    }

    TEST(AnalysisJob, CancelStopsTheScan) {
        const auto drive = specialDrive();
        if (!drive) {
            GTEST_SKIP() << "needs administrator rights and an ICD-TEST volume with the special scenario";
        }
        services::ThreadPool pool(4);
        auto output = std::make_shared<services::AnalysisOutput>();
        core::CancelSource source;
        RecordingSink sink(&source, 4); // Cancel while files are being read.
        services::JobContext context{source.token(), sink, pool};
        auto job = services::makeAnalysisJob(*drive, {16, 9}, output);
        EXPECT_THROW(job(context), core::OperationCancelled);
        EXPECT_FALSE(output->takeSummary().has_value());
    }

} // namespace
