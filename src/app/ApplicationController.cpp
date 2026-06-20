#include "ApplicationController.h"

#include "../analysis/DriveAnalysisService.h"
#include "../analysis/FakeAnalysisService.h"
#include "../classification/FileClassifier.h"
#include "../optimization/MovePlanner.h"
#include "../optimization/PlacementPlanner.h"
#include "../optimization/ProfileCatalog.h"
#include "../platform/windows/DriveEnumerator.h"
#include "../support/Logger.h"

#include <exception>
#include <utility>

namespace icd {

namespace {
std::wstring ToWide(const char* text)
{
    std::wstring result;
    if (text == nullptr) {
        return result;
    }

    while (*text != '\0') {
        result.push_back(static_cast<wchar_t>(*text));
        ++text;
    }
    return result;
}
} // namespace

ApplicationController::ApplicationController()
{
    profiles = ProfileCatalog().CreateDefaultProfiles();
    if (!profiles.empty()) {
        activeProfile = profiles.front();
    }
}

ApplicationController::~ApplicationController()
{
    CancelActiveJob();
    ClearCallbacks();
}

void ApplicationController::SetProgressCallback(ProgressCallback callback)
{
    std::lock_guard lock(callbackMutex);
    progressCallback = std::move(callback);
}

void ApplicationController::SetCompletionCallback(CompletionCallback callback)
{
    std::lock_guard lock(callbackMutex);
    completionCallback = std::move(callback);
}

void ApplicationController::SetErrorCallback(ErrorCallback callback)
{
    std::lock_guard lock(callbackMutex);
    errorCallback = std::move(callback);
}

void ApplicationController::ClearCallbacks()
{
    std::lock_guard lock(callbackMutex);
    progressCallback = nullptr;
    completionCallback = nullptr;
    errorCallback = nullptr;
}

std::vector<DriveInfo> ApplicationController::EnumerateDrives() const
{
    win::DriveEnumerator enumerator;
    return enumerator.Enumerate();
}

bool ApplicationController::StartFakeAnalysis()
{
    if (activeJob.IsRunning()) {
        NotifyError(L"Analysis is already running.");
        return false;
    }

    Logger::Info(L"Starting synthetic analysis job.");
    const bool started = activeJob.Start([this](const std::atomic_bool& cancellationRequested) {
        try {
            FakeAnalysisService service;
            AnalysisResult result = service.Run(cancellationRequested, [this](const JobProgress& progress) {
                NotifyProgress(progress);
            });

            if (cancellationRequested.load()) {
                JobProgress cancelled;
                cancelled.state = JobState::Cancelled;
                cancelled.percentComplete = 0.0;
                cancelled.statusMessage = L"Synthetic analysis cancelled.";
                cancelled.cancellationRequested = true;
                NotifyProgress(cancelled);
                Logger::Info(L"Synthetic analysis job cancelled.");
                return;
            }

            JobProgress completed;
            completed.state = JobState::Completed;
            completed.percentComplete = 100.0;
            completed.statusMessage = L"Synthetic analysis complete.";
            result = FileClassifier().Classify(std::move(result));
            NotifyProgress(completed);
            NotifyCompletion(result);
            Logger::Info(L"Synthetic analysis job completed.");
        } catch (const std::exception& ex) {
            const std::wstring message = L"Synthetic analysis failed: " + ToWide(ex.what());
            Logger::Error(message);
            NotifyError(message);
        } catch (...) {
            const std::wstring message = L"Synthetic analysis failed with an unknown error.";
            Logger::Error(message);
            NotifyError(message);
        }
    });

    if (!started) {
        NotifyError(L"Unable to start analysis job.");
    }

    return started;
}

// Starts a cancellable worker job that runs read-only analysis away from the UI thread.
bool ApplicationController::StartDriveAnalysis(const DriveInfo& drive)
{
    if (activeJob.IsRunning()) {
        NotifyError(L"Analysis is already running.");
        return false;
    }

    if (!drive.capabilities.canAnalyze) {
        NotifyError(L"Selected drive is not enabled for read-only analysis.");
        return false;
    }

    Logger::Info(L"Starting read-only drive analysis for " + drive.rootPath);
    const bool started = activeJob.Start([this, drive](const std::atomic_bool& cancellationRequested) {
        try {
            DriveAnalysisService service;
            AnalysisResult result = service.Run(drive, cancellationRequested, [this](const JobProgress& progress) {
                NotifyProgress(progress);
            });

            if (cancellationRequested.load()) {
                result.stats.cancelled = true;
                JobProgress cancelled;
                cancelled.state = JobState::Cancelled;
                cancelled.percentComplete = 0.0;
                cancelled.statusMessage = L"Drive analysis cancelled.";
                cancelled.cancellationRequested = true;
                NotifyProgress(cancelled);
                Logger::Info(L"Read-only drive analysis cancelled.");
                return;
            }

            result = FileClassifier().Classify(std::move(result));

            {
                std::lock_guard lock(analysisMutex);
                completedAnalyses[result.drive.rootPath] = result;
                placementPlans.erase(result.drive.rootPath);
                movePlans.erase(result.drive.rootPath);
            }

            JobProgress completed;
            completed.state = JobState::Completed;
            completed.percentComplete = 100.0;
            completed.statusMessage = L"Drive analysis complete.";
            completed.itemsProcessed = result.stats.scannedFiles;
            NotifyProgress(completed);
            NotifyCompletion(result);
            Logger::Info(L"Read-only drive analysis completed for " + result.drive.rootPath);
        } catch (const std::exception& ex) {
            const std::wstring message = L"Drive analysis failed: " + ToWide(ex.what());
            Logger::Error(message);
            NotifyError(message);
        } catch (...) {
            const std::wstring message = L"Drive analysis failed with an unknown error.";
            Logger::Error(message);
            NotifyError(message);
        }
    });

    if (!started) {
        NotifyError(L"Unable to start drive analysis job.");
    }

    return started;
}

void ApplicationController::RequestCancelActiveJob()
{
    if (activeJob.IsRunning()) {
        Logger::Info(L"Requesting cancellation of active analysis job.");
        activeJob.RequestCancel();
    }
}

void ApplicationController::CancelActiveJob()
{
    if (activeJob.IsRunning()) {
        Logger::Info(L"Cancelling active analysis job.");
        activeJob.RequestCancel();
    }

    activeJob.Join();
}

bool ApplicationController::IsJobRunning() const
{
    return activeJob.IsRunning();
}

std::vector<AnalysisResult> ApplicationController::GetAnalysisSnapshots() const
{
    std::lock_guard lock(analysisMutex);
    std::vector<AnalysisResult> snapshots;
    snapshots.reserve(completedAnalyses.size());
    for (const auto& [root, result] : completedAnalyses) {
        snapshots.push_back(result);
    }
    return snapshots;
}

std::vector<OptimizationProfile> ApplicationController::GetProfiles() const
{
    std::lock_guard lock(profileMutex);
    return profiles;
}

OptimizationProfile ApplicationController::GetActiveProfile() const
{
    std::lock_guard lock(profileMutex);
    return activeProfile;
}

void ApplicationController::SetActiveProfile(const OptimizationProfile& profile)
{
    std::lock_guard lock(profileMutex);
    activeProfile = profile;
    for (OptimizationProfile& stored : profiles) {
        if (stored.mode == profile.mode) {
            stored = profile;
            return;
        }
    }
    profiles.push_back(profile);
}

std::optional<PlacementPlan> ApplicationController::BuildPlacementPlan(const std::wstring& driveRoot)
{
    AnalysisResult analysis;
    {
        std::lock_guard lock(analysisMutex);
        const auto found = completedAnalyses.find(driveRoot);
        if (found == completedAnalyses.end()) {
            return std::nullopt;
        }
        analysis = found->second;
    }

    OptimizationProfile profile = GetActiveProfile();
    PlacementPlan plan = PlacementPlanner().Build(analysis, profile);

    {
        std::lock_guard lock(analysisMutex);
        placementPlans[driveRoot] = plan;
        movePlans.erase(driveRoot);
    }

    return plan;
}

std::optional<PlacementPlan> ApplicationController::GetPlacementPlan(const std::wstring& driveRoot) const
{
    std::lock_guard lock(analysisMutex);
    const auto found = placementPlans.find(driveRoot);
    if (found == placementPlans.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<MovePlan> ApplicationController::BuildMovePlan(const std::wstring& driveRoot)
{
    AnalysisResult analysis;
    std::optional<PlacementPlan> placement;
    {
        std::lock_guard lock(analysisMutex);
        const auto analysisFound = completedAnalyses.find(driveRoot);
        if (analysisFound == completedAnalyses.end()) {
            return std::nullopt;
        }
        analysis = analysisFound->second;

        const auto placementFound = placementPlans.find(driveRoot);
        if (placementFound != placementPlans.end()) {
            placement = placementFound->second;
        }
    }

    if (!placement.has_value()) {
        placement = BuildPlacementPlan(driveRoot);
        if (!placement.has_value()) {
            return std::nullopt;
        }
    }

    OptimizationProfile profile = GetActiveProfile();
    MovePlan plan = MovePlanner().Build(analysis, *placement, profile);

    {
        std::lock_guard lock(analysisMutex);
        movePlans[driveRoot] = plan;
    }

    return plan;
}

std::optional<MovePlan> ApplicationController::GetMovePlan(const std::wstring& driveRoot) const
{
    std::lock_guard lock(analysisMutex);
    const auto found = movePlans.find(driveRoot);
    if (found == movePlans.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ApplicationController::NotifyProgress(const JobProgress& progress)
{
    ProgressCallback callback;
    {
        std::lock_guard lock(callbackMutex);
        callback = progressCallback;
    }

    if (callback) {
        callback(progress);
    }
}

void ApplicationController::NotifyCompletion(const AnalysisResult& result)
{
    CompletionCallback callback;
    {
        std::lock_guard lock(callbackMutex);
        callback = completionCallback;
    }

    if (callback) {
        callback(result);
    }
}

void ApplicationController::NotifyError(const std::wstring& message)
{
    ErrorCallback callback;
    {
        std::lock_guard lock(callbackMutex);
        callback = errorCallback;
    }

    if (callback) {
        callback(message);
    }
}

} // namespace icd
