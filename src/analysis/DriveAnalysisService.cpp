#include "DriveAnalysisService.h"

#include "../platform/windows/VolumeQueries.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <sstream>
#include <system_error>

namespace icd {

namespace {
// Converts Win32 timestamps into the model's filesystem clock type.
std::filesystem::file_time_type ToFileTimeType(const FILETIME& fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;

    constexpr std::uint64_t windowsToUnixEpochTicks = 116444736000000000ull;
    if (value.QuadPart <= windowsToUnixEpochTicks) {
        return std::filesystem::file_time_type{};
    }

    using FileTime = std::filesystem::file_time_type;
    return FileTime(FileTime::duration(static_cast<FileTime::duration::rep>(value.QuadPart)));
}

std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return extension;
}

// Assigns a coarse file type for analysis summaries before strategy classification exists.
FileMetadata::FileType ClassifyFileType(const std::filesystem::path& path, std::uint32_t attributes)
{
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
        return FileMetadata::FileType::System;
    }

    const std::wstring extension = LowerExtension(path);
    if (extension == L".exe" || extension == L".dll" || extension == L".sys" || extension == L".msi") {
        return FileMetadata::FileType::Executable;
    }
    if (extension == L".zip" || extension == L".7z" || extension == L".rar" || extension == L".tar" ||
        extension == L".gz" || extension == L".iso") {
        return FileMetadata::FileType::Archive;
    }
    if (extension == L".mp4" || extension == L".mkv" || extension == L".avi" || extension == L".mov" ||
        extension == L".mp3" || extension == L".flac" || extension == L".jpg" || extension == L".png") {
        return FileMetadata::FileType::Media;
    }
    if (extension == L".txt" || extension == L".pdf" || extension == L".doc" || extension == L".docx" ||
        extension == L".xls" || extension == L".xlsx" || extension == L".cpp" || extension == L".h") {
        return FileMetadata::FileType::Document;
    }

    return FileMetadata::FileType::Other;
}

FileMetadata::AttributeFlags BuildAttributeFlags(std::uint32_t attributes, bool extentsAvailable)
{
    FileMetadata::AttributeFlags flags;
    flags.hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    flags.system = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    flags.reparsePoint = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    flags.sparse = (attributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
    flags.compressed = (attributes & FILE_ATTRIBUTE_COMPRESSED) != 0;
    flags.encrypted = (attributes & FILE_ATTRIBUTE_ENCRYPTED) != 0;
    flags.extentsAvailable = extentsAvailable;
    flags.riskyOrUnmovable = flags.system || flags.reparsePoint || flags.sparse || flags.compressed || flags.encrypted;
    return flags;
}

bool QueryFileData(const std::filesystem::path& path, WIN32_FILE_ATTRIBUTE_DATA& data)
{
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != FALSE;
}

byte_count64_t FileSizeFromData(const WIN32_FILE_ATTRIBUTE_DATA& data)
{
    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    return byte_count64_t(size.QuadPart);
}

// Builds approximate logical performance zones until real measured geometry exists.
DiskGeometry BuildGeometry(const DriveInfo& drive)
{
    const std::uint64_t bytesPerSector = drive.bytesPerSector.getValue() == 0 ? 512 : drive.bytesPerSector.getValue();
    const std::uint64_t totalSectors = drive.volume.totalBytes.getValue() / bytesPerSector;
    const std::uint64_t firstZoneEnd = totalSectors / 3;
    const std::uint64_t secondZoneEnd = (totalSectors * 2) / 3;

    std::vector<DiskGeometry::PerformanceZone> zones{
        {index64_t(0), index64_t(firstZoneEnd), megabyte_sec_t(220.0), megabyte_sec_t(180.0),
         std::chrono::milliseconds(8), std::chrono::milliseconds(4)},
        {index64_t(firstZoneEnd), index64_t(secondZoneEnd), megabyte_sec_t(170.0), megabyte_sec_t(150.0),
         std::chrono::milliseconds(9), std::chrono::milliseconds(4)},
        {index64_t(secondZoneEnd), index64_t(totalSectors), megabyte_sec_t(120.0), megabyte_sec_t(110.0),
         std::chrono::milliseconds(10), std::chrono::milliseconds(4)},
    };

    return DiskGeometry(sector_count64_t(totalSectors),
                        byte_count64_t(bytesPerSector),
                        sector_count32_t(63),
                        count32_t(255),
                        {},
                        std::move(zones));
}

void ReportProgress(const DriveAnalysisService::ProgressCallback& callback,
                    double percent,
                    std::wstring message,
                    std::wstring currentItem,
                    const AnalysisResult::AnalysisStats& stats,
                    const std::atomic_bool& cancellationRequested)
{
    if (!callback) {
        return;
    }

    JobProgress progress;
    progress.state = cancellationRequested.load() ? JobState::Cancelling : JobState::Running;
    progress.percentComplete = percent;
    progress.statusMessage = std::move(message);
    progress.currentItem = std::move(currentItem);
    progress.itemsProcessed = stats.scannedFiles;
    progress.totalItems = count64_t(0);
    progress.cancellationRequested = cancellationRequested.load();
    callback(progress);
}

void Increment(count64_t& value)
{
    value += count64_t(1);
}

std::wstring BuildSummary(const AnalysisResult& result)
{
    std::wstringstream summary;
    summary << L"Analysis complete for " << result.drive.rootPath << L": ";
    summary << result.stats.scannedFiles.getValue() << L" files scanned, ";
    summary << result.stats.fragmentedFiles.getValue() << L" fragmented files, ";
    summary << result.stats.filesWithExtents.getValue() << L" files with extents, ";
    summary << result.stats.freeSpaceBlocks.getValue() << L" free-space blocks.";
    if (!result.stats.freeSpaceMapAvailable) {
        summary << L" Free-space bitmap unavailable.";
    }
    if (result.stats.cancelled) {
        summary << L" Cancelled before completion.";
    }
    return summary.str();
}
} // namespace

// Performs the read-only scan and keeps per-item failures from aborting the whole analysis.
AnalysisResult DriveAnalysisService::Run(const DriveInfo& drive,
                                         const std::atomic_bool& cancellationRequested,
                                         const ProgressCallback& progressCallback) const
{
    AnalysisResult result;
    result.drive = drive;
    result.volume = drive.volume;
    result.geometry = BuildGeometry(drive);

    ReportProgress(progressCallback, 0.0, L"Reading free-space bitmap", drive.rootPath, result.stats, cancellationRequested);
    const win::FreeSpaceQueryResult freeSpace = win::QueryVolumeFreeSpace(drive.rootPath);
    result.stats.freeSpaceMapAvailable = freeSpace.available;
    if (freeSpace.available) {
        result.freeSpace = freeSpace.freeSpace;
        result.stats.freeSpaceBlocks = count64_t(freeSpace.freeSpace.GetFragmentationCount().getValue());
        result.stats.largestFreeBlockSectors = freeSpace.largestFreeBlockSectors;
    }

    const std::filesystem::path root(drive.rootPath);
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, ec);
    std::filesystem::recursive_directory_iterator end;
    if (ec) {
        Increment(result.stats.inaccessibleFiles);
        result.summary = L"Unable to enumerate drive root.";
        return result;
    }

    std::uint64_t visitedEntries = 0;
    ReportProgress(progressCallback, 1.0, L"Scanning files", drive.rootPath, result.stats, cancellationRequested);

    while (iterator != end) {
        if (cancellationRequested.load()) {
            result.stats.cancelled = true;
            break;
        }

        const std::filesystem::directory_entry entry = *iterator;
        ++visitedEntries;

        WIN32_FILE_ATTRIBUTE_DATA data{};
        const bool hasFileData = QueryFileData(entry.path(), data);
        const std::uint32_t attributes = hasFileData ? data.dwFileAttributes : GetFileAttributesW(entry.path().c_str());
        const bool isReparsePoint = attributes != INVALID_FILE_ATTRIBUTES &&
                                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (isReparsePoint && entry.is_directory(ec)) {
            iterator.disable_recursion_pending();
            Increment(result.stats.skippedFiles);
            iterator.increment(ec);
            if (ec) {
                Increment(result.stats.inaccessibleFiles);
                ec.clear();
            }
            continue;
        }

        const bool isRegularFile = entry.is_regular_file(ec);
        if (ec) {
            Increment(result.stats.inaccessibleFiles);
            ec.clear();
            iterator.increment(ec);
            continue;
        }

        if (!isRegularFile) {
            iterator.increment(ec);
            if (ec) {
                Increment(result.stats.inaccessibleFiles);
                ec.clear();
            }
            continue;
        }

        if (!hasFileData) {
            Increment(result.stats.inaccessibleFiles);
            iterator.increment(ec);
            if (ec) {
                Increment(result.stats.inaccessibleFiles);
                ec.clear();
            }
            continue;
        }

        win::FileExtentsResult extents;
        if (drive.capabilities.canQueryExtents) {
            extents = win::QueryFileExtents(entry.path());
        }

        const FileMetadata::AttributeFlags flags = BuildAttributeFlags(attributes, extents.available);
        result.files.emplace_back(entry.path(),
                                  FileSizeFromData(data),
                                  std::move(extents.fragments),
                                  ToFileTimeType(data.ftLastAccessTime),
                                  ToFileTimeType(data.ftCreationTime),
                                  ToFileTimeType(data.ftLastWriteTime),
                                  ClassifyFileType(entry.path(), attributes),
                                  attributes,
                                  flags);

        Increment(result.stats.scannedFiles);
        if (flags.extentsAvailable) {
            Increment(result.stats.filesWithExtents);
            const FileMetadata& file = result.files.back();
            result.stats.totalFragments += count64_t(static_cast<std::uint64_t>(file.GetFragments().size()));
            if (file.IsFragmented()) {
                Increment(result.stats.fragmentedFiles);
            }
        }

        if (visitedEntries % 100 == 0) {
            const double percent = 5.0 + static_cast<double>(visitedEntries % 9000) / 100.0;
            ReportProgress(progressCallback,
                           percent > 95.0 ? 95.0 : percent,
                           L"Scanning files",
                           entry.path().wstring(),
                           result.stats,
                           cancellationRequested);
        }

        iterator.increment(ec);
        if (ec) {
            Increment(result.stats.inaccessibleFiles);
            ec.clear();
        }
    }

    result.summary = BuildSummary(result);
    return result;
}

} // namespace icd
