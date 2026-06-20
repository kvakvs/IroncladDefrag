#include "FakeAnalysisService.h"

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace icd {

namespace {
void ReportProgress(const FakeAnalysisService::ProgressCallback& progressCallback,
                    double percent,
                    std::wstring status,
                    const std::atomic_bool& cancellationRequested)
{
    if (!progressCallback) {
        return;
    }

    JobProgress progress;
    progress.state = JobState::Running;
    progress.percentComplete = percent;
    progress.statusMessage = std::move(status);
    progress.totalItems = count64_t(10);
    progress.itemsProcessed = count64_t(static_cast<std::uint64_t>(percent / 10.0));
    progress.cancellationRequested = cancellationRequested.load();
    progressCallback(progress);
}

AnalysisResult BuildSyntheticResult()
{
    const auto now = std::filesystem::file_time_type::clock::now();

    std::vector<FileMetadata> files;
    files.emplace_back(L"X:\\Media\\movie.mkv",
                       byte_count64_t(3ull * 1024ull * 1024ull * 1024ull),
                       std::vector<FileMetadata::FragmentLocation>{{index64_t(150000), count64_t(700000)}},
                       now,
                       now,
                       now,
                       FileMetadata::FileType::Media);
    files.emplace_back(L"X:\\Projects\\source.cpp",
                       byte_count64_t(96ull * 1024ull),
                       std::vector<FileMetadata::FragmentLocation>{{index64_t(32000), count64_t(12)},
                                                                   {index64_t(104000), count64_t(8)}},
                       now,
                       now,
                       now,
                       FileMetadata::FileType::Document);
    files.emplace_back(L"X:\\Archives\\backup.7z",
                       byte_count64_t(512ull * 1024ull * 1024ull),
                       std::vector<FileMetadata::FragmentLocation>{{index64_t(850000), count64_t(120000)}},
                       now,
                       now,
                       now,
                       FileMetadata::FileType::Archive);

    std::vector<DiskGeometry::PerformanceZone> performanceZones{
        {index64_t(0), index64_t(399999), megabyte_sec_t(220.0), megabyte_sec_t(180.0), std::chrono::milliseconds(8), std::chrono::milliseconds(4)},
        {index64_t(400000), index64_t(799999), megabyte_sec_t(170.0), megabyte_sec_t(150.0), std::chrono::milliseconds(9), std::chrono::milliseconds(4)},
        {index64_t(800000), index64_t(999999), megabyte_sec_t(120.0), megabyte_sec_t(110.0), std::chrono::milliseconds(10), std::chrono::milliseconds(4)},
    };

    AnalysisResult result;
    result.drive = {L"X:\\", L"Synthetic data drive", DriveKind::Mechanical, true, false, true};
    result.volume = {L"DemoData", L"NTFS", byte_count64_t(512ull * 1024ull * 1024ull * 1024ull),
                     byte_count64_t(128ull * 1024ull * 1024ull * 1024ull), byte_count64_t(4096)};
    result.geometry = DiskGeometry(sector_count64_t(1000000),
                                   byte_count64_t(512),
                                   sector_count32_t(63),
                                   count32_t(255),
                                   {},
                                   std::move(performanceZones));
    result.files = std::move(files);
    result.freeSpace = FreeSpaceMap({{index64_t(200000), count64_t(50000)},
                                     {index64_t(760000), count64_t(90000)},
                                     {index64_t(970000), count64_t(30000)}});
    result.synthetic = true;
    result.summary = L"Synthetic analysis complete: 3 files, 3 free-space blocks.";
    return result;
}
} // namespace

AnalysisResult FakeAnalysisService::Run(const std::atomic_bool& cancellationRequested,
                                        const ProgressCallback& progressCallback) const
{
    for (int step = 0; step <= 10; ++step) {
        if (cancellationRequested.load()) {
            break;
        }

        ReportProgress(progressCallback,
                       static_cast<double>(step) * 10.0,
                       step == 0 ? L"Starting synthetic analysis" : L"Running synthetic analysis",
                       cancellationRequested);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    return BuildSyntheticResult();
}

} // namespace icd
