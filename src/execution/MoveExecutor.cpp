#include "../precompiled.h"
#include "MoveExecutor.h"

#include "../platform/windows/VolumeMoveOperations.h"
#include "../platform/windows/VolumeQueries.h"
#include "../support/Logger.h"

#include <cwctype>
#include <limits>
#include <sstream>

namespace icd {

namespace {
constexpr std::uint64_t MaxPhase6Operations = 10;
constexpr std::uint64_t MaxPhase6Bytes = 1024ull * 1024ull * 1024ull;

std::wstring LowerAsciiPath(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::uint64_t FragmentClusterCount(const std::vector<FileMetadata::FragmentLocation>& fragments)
{
    std::uint64_t total = 0;
    for (const auto& fragment : fragments) {
        total += fragment.clusterCount.getValue();
    }
    return total;
}

bool IsPathUnderDrive(const std::filesystem::path& path, const std::wstring& rootPath)
{
    const std::wstring file = LowerAsciiPath(path.wstring());
    const std::wstring root = LowerAsciiPath(rootPath);
    return file.rfind(root, 0) == 0;
}

bool HasUnsafeAttributes(std::uint32_t attributes)
{
    constexpr std::uint32_t unsafe = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE |
                                     FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED;
    return (attributes & unsafe) != 0;
}

byte_count64_t SizeFromData(const WIN32_FILE_ATTRIBUTE_DATA& data)
{
    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    return byte_count64_t(size.QuadPart);
}

void AddLog(MoveExecutionResult& result, std::wstring message)
{
    Logger::Info(message);
    result.log.push_back({std::chrono::system_clock::now(), std::move(message)});
}

void ReportProgress(const MoveExecutor::ProgressCallback& callback,
                    double percent,
                    std::wstring message,
                    std::wstring currentItem,
                    count64_t processed,
                    count64_t total,
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
    progress.itemsProcessed = processed;
    progress.totalItems = total;
    progress.cancellationRequested = cancellationRequested.load();
    callback(progress);
}

MoveExecutionOperationResult MakeSkipped(const MoveOperation& operation,
                                         MoveExecutionSkipReason reason,
                                         std::wstring detail)
{
    MoveExecutionOperationResult result;
    result.fileIndex = operation.fileIndex;
    result.filePath = operation.filePath;
    result.state = MoveExecutionState::Skipped;
    result.reason = reason;
    result.bytesConsidered = operation.estimatedBytes;
    result.fragmentsBefore = operation.fragmentCountBefore;
    result.detail = std::move(detail);
    return result;
}

void AddSkipped(MoveExecutionResult& result, const MoveOperation& operation, MoveExecutionSkipReason reason, std::wstring detail)
{
    MoveExecutionOperationResult skipped = MakeSkipped(operation, reason, std::move(detail));
    AddLog(result, L"Skipped " + skipped.filePath.wstring() + L": " + skipped.detail);
    result.operationResults.push_back(std::move(skipped));
    result.metrics.skippedOperations += count64_t(1);
}

void AddFailed(MoveExecutionResult& result,
               const MoveOperation& operation,
               MoveExecutionSkipReason reason,
               std::uint32_t windowsError,
               std::wstring detail)
{
    MoveExecutionOperationResult failed;
    failed.fileIndex = operation.fileIndex;
    failed.filePath = operation.filePath;
    failed.state = MoveExecutionState::Failed;
    failed.reason = reason;
    failed.windowsError = windowsError;
    failed.bytesConsidered = operation.estimatedBytes;
    failed.fragmentsBefore = operation.fragmentCountBefore;
    failed.detail = std::move(detail);
    AddLog(result, L"Failed " + failed.filePath.wstring() + L": " + failed.detail);
    result.operationResults.push_back(std::move(failed));
    result.metrics.failedOperations += count64_t(1);
}

bool RevalidateOperation(const AnalysisResult& analysis,
                         const MoveOperation& operation,
                         MoveExecutionResult& result,
                         const FileMetadata*& analysedFile,
                         win::FileExtentsResult& currentExtents)
{
    if (operation.fileIndex >= analysis.files.size()) {
        AddSkipped(result, operation, MoveExecutionSkipReason::InvalidFileIndex, L"file index is outside analysis snapshot");
        return false;
    }

    analysedFile = &analysis.files[operation.fileIndex];
    if (!IsPathUnderDrive(operation.filePath, analysis.drive.rootPath)) {
        AddSkipped(result, operation, MoveExecutionSkipReason::PathOutsideDrive, L"planned path is outside analysed drive");
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(operation.filePath.c_str(), GetFileExInfoStandard, &data)) {
        AddSkipped(result, operation, MoveExecutionSkipReason::FileMissing, L"file metadata is no longer readable");
        return false;
    }

    if (SizeFromData(data).getValue() != operation.estimatedBytes.getValue() ||
        SizeFromData(data).getValue() != analysedFile->GetSize().getValue()) {
        AddSkipped(result, operation, MoveExecutionSkipReason::FileChanged, L"file size changed after analysis");
        return false;
    }

    if (HasUnsafeAttributes(data.dwFileAttributes) || analysedFile->IsRiskyOrUnmovable()) {
        AddSkipped(result, operation, MoveExecutionSkipReason::UnsafeAttributes, L"file has unsafe movement attributes");
        return false;
    }

    currentExtents = win::QueryFileExtents(operation.filePath);
    if (!currentExtents.available || currentExtents.fragments.empty()) {
        AddSkipped(result, operation, MoveExecutionSkipReason::MissingExtents, L"current extents are unavailable");
        return false;
    }

    const std::uint64_t currentClusters = FragmentClusterCount(currentExtents.fragments);
    const std::uint64_t analysedClusters = FragmentClusterCount(analysedFile->GetFragments());
    if (currentClusters != analysedClusters ||
        currentExtents.fragments.front().startCluster.getValue() != operation.sourceStartCluster.getValue() ||
        currentExtents.fragments.size() != analysedFile->GetFragments().size()) {
        AddSkipped(result, operation, MoveExecutionSkipReason::FileChanged, L"file extents changed after analysis");
        return false;
    }

    return true;
}

bool VerifyMove(const MoveOperation& operation, MoveExecutionOperationResult& operationResult)
{
    const win::FileExtentsResult after = win::QueryFileExtents(operation.filePath);
    operationResult.fragmentsAfter = count64_t(static_cast<std::uint64_t>(after.fragments.size()));
    if (!after.available || after.fragments.empty()) {
        operationResult.detail = L"post-move extent verification failed";
        return false;
    }

    if (after.fragments.front().startCluster.getValue() != operation.targetStartCluster.getValue()) {
        operationResult.detail = L"post-move first cluster does not match the planned target";
        return false;
    }

    return true;
}

std::wstring BuildSummary(const MoveExecutionResult& result)
{
    std::wstringstream summary;
    if (result.blocked) {
        summary << L"Move execution blocked: " << result.privileges.message;
        return summary.str();
    }

    summary << L"Move execution ";
    summary << (result.cancelled ? L"cancelled" : L"complete");
    summary << L": " << result.metrics.movedOperations.getValue() << L" moved, "
            << result.metrics.skippedOperations.getValue() << L" skipped, "
            << result.metrics.failedOperations.getValue() << L" failed, "
            << result.metrics.verificationFailures.getValue() << L" verification failures.";
    return summary.str();
}
} // namespace

// Runs a conservative, bounded execution pass with validation before every operation.
MoveExecutionResult MoveExecutor::Execute(const AnalysisResult& analysis,
                                          const MovePlan& plan,
                                          const std::atomic_bool& cancellationRequested,
                                          const ProgressCallback& progressCallback) const
{
    MoveExecutionResult result;
    result.privileges = win::ProbeMovePrivileges(analysis.drive.rootPath);

    if (!result.privileges.canMoveFiles) {
        result.blocked = true;
        result.summary = BuildSummary(result);
        AddLog(result, result.summary);
        return result;
    }

    if (plan.profile.settings.dryRunOnly) {
        result.blocked = true;
        result.summary = L"Move execution blocked: the selected profile is still dry-run only.";
        AddLog(result, result.summary);
        return result;
    }

    if (plan.impossible || plan.operations.empty()) {
        result.blocked = true;
        result.summary = plan.impossible ? L"Move execution blocked: the move plan is impossible."
                                         : L"Move execution blocked: the move plan has no operations.";
        AddLog(result, result.summary);
        return result;
    }

    const std::uint64_t planLimit =
        plan.profile.settings.maximumBytesToMove.getValue() == 0
            ? MaxPhase6Bytes
            : (std::min)(MaxPhase6Bytes, plan.profile.settings.maximumBytesToMove.getValue());
    std::uint64_t bytesConsidered = 0;
    std::uint64_t processed = 0;
    const std::uint64_t total = (std::min)(MaxPhase6Operations, static_cast<std::uint64_t>(plan.operations.size()));

    ReportProgress(progressCallback,
                   0.0,
                   L"Starting move execution",
                   analysis.drive.rootPath,
                   count64_t(0),
                   count64_t(total),
                   cancellationRequested);
    AddLog(result, L"Starting bounded move execution for " + analysis.drive.rootPath);

    for (const MoveOperation& operation : plan.operations) {
        if (processed >= MaxPhase6Operations || bytesConsidered + operation.estimatedBytes.getValue() > planLimit) {
            AddSkipped(result, operation, MoveExecutionSkipReason::ExecutionLimitReached, L"Phase 6 execution limit reached");
            continue;
        }

        if (cancellationRequested.load()) {
            result.cancelled = true;
            AddSkipped(result, operation, MoveExecutionSkipReason::CancellationRequested, L"execution cancelled before this move");
            continue;
        }

        ++processed;
        bytesConsidered += operation.estimatedBytes.getValue();
        result.metrics.attemptedOperations += count64_t(1);
        ReportProgress(progressCallback,
                       (static_cast<double>(processed - 1) / static_cast<double>(total)) * 100.0,
                       L"Executing move plan",
                       operation.filePath.wstring(),
                       count64_t(processed - 1),
                       count64_t(total),
                       cancellationRequested);

        const FileMetadata* analysedFile = nullptr;
        win::FileExtentsResult currentExtents;
        if (!RevalidateOperation(analysis, operation, result, analysedFile, currentExtents)) {
            continue;
        }

        if (operation.clusterCount.getValue() > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())) {
            AddSkipped(result,
                       operation,
                       MoveExecutionSkipReason::UnsupportedClusterCount,
                       L"planned cluster count is too large for FSCTL_MOVE_FILE");
            continue;
        }

        win::FileMoveRequest request;
        request.rootPath = analysis.drive.rootPath;
        request.filePath = operation.filePath;
        request.startingVcn = index64_t(0);
        request.targetLcn = operation.targetStartCluster;
        request.clusterCount = operation.clusterCount;

        const win::FileMoveResult moveResult = win::MoveFileClusters(request);
        if (!moveResult.succeeded) {
            AddFailed(result, operation, MoveExecutionSkipReason::MoveFailed, moveResult.errorCode, moveResult.message);
            continue;
        }

        MoveExecutionOperationResult operationResult;
        operationResult.fileIndex = operation.fileIndex;
        operationResult.filePath = operation.filePath;
        operationResult.state = MoveExecutionState::Moved;
        operationResult.bytesConsidered = operation.estimatedBytes;
        operationResult.fragmentsBefore = count64_t(static_cast<std::uint64_t>(currentExtents.fragments.size()));
        operationResult.detail = L"move completed and verified";

        if (!VerifyMove(operation, operationResult)) {
            operationResult.state = MoveExecutionState::VerificationFailed;
            operationResult.reason = MoveExecutionSkipReason::VerificationFailed;
            result.metrics.verificationFailures += count64_t(1);
            result.metrics.failedOperations += count64_t(1);
            AddLog(result, L"Verification failed " + operation.filePath.wstring() + L": " + operationResult.detail);
        } else {
            result.metrics.movedOperations += count64_t(1);
            result.metrics.movedBytes += operation.estimatedBytes;
            AddLog(result, L"Moved " + operation.filePath.wstring());
        }

        result.operationResults.push_back(std::move(operationResult));
    }

    ReportProgress(progressCallback,
                   100.0,
                   result.cancelled ? L"Move execution cancelled" : L"Move execution complete",
                   analysis.drive.rootPath,
                   count64_t(processed),
                   count64_t(total),
                   cancellationRequested);
    result.summary = BuildSummary(result);
    AddLog(result, result.summary);
    return result;
}

} // namespace icd
