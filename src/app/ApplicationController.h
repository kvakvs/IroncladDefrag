#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../model/DomainTypes.h"
#include "BackgroundJob.h"

namespace icd {

// Orchestrates UI-facing drive discovery, analysis jobs, callbacks, and cached snapshots.
class ApplicationController {
public:
    using ProgressCallback = std::function<void(const JobProgress&)>;
    using CompletionCallback = std::function<void(const AnalysisResult&)>;
    using ExecutionCompletionCallback = std::function<void(const MoveExecutionResult&)>;
    using ErrorCallback = std::function<void(const std::wstring&)>;

    ApplicationController();
    ~ApplicationController();

    ApplicationController(const ApplicationController&) = delete;
    ApplicationController& operator=(const ApplicationController&) = delete;

    void SetProgressCallback(ProgressCallback callback);
    void SetCompletionCallback(CompletionCallback callback);
    void SetExecutionCompletionCallback(ExecutionCompletionCallback callback);
    void SetErrorCallback(ErrorCallback callback);
    void ClearCallbacks();

    std::vector<DriveInfo> EnumerateDrives() const;
    bool StartFakeAnalysis();
    bool StartDriveAnalysis(const DriveInfo& drive);
    void RequestCancelActiveJob();
    void CancelActiveJob();
    bool IsJobRunning() const;
    std::vector<AnalysisResult> GetAnalysisSnapshots() const;
    std::vector<OptimizationProfile> GetProfiles() const;
    OptimizationProfile GetActiveProfile() const;
    void SetActiveProfile(const OptimizationProfile& profile);
    std::optional<PlacementPlan> BuildPlacementPlan(const std::wstring& driveRoot);
    std::optional<PlacementPlan> GetPlacementPlan(const std::wstring& driveRoot) const;
    std::optional<MovePlan> BuildMovePlan(const std::wstring& driveRoot);
    std::optional<MovePlan> GetMovePlan(const std::wstring& driveRoot) const;
    bool HasMovePlan(const std::wstring& driveRoot) const;
    bool HasExecutableMovePlan(const std::wstring& driveRoot) const;
    MoveExecutionPrivilegeStatus GetMoveExecutionPrivilegeStatus(const std::wstring& driveRoot) const;
    bool RelaunchElevatedForExecution() const;
    bool StartMovePlanExecution(const std::wstring& driveRoot);
    std::optional<MoveExecutionResult> GetMoveExecutionResult(const std::wstring& driveRoot) const;

private:
    void NotifyProgress(const JobProgress& progress);
    void NotifyCompletion(const AnalysisResult& result);
    void NotifyExecutionCompletion(const MoveExecutionResult& result);
    void NotifyError(const std::wstring& message);

    mutable std::mutex callbackMutex;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    ExecutionCompletionCallback executionCompletionCallback;
    ErrorCallback errorCallback;
    BackgroundJob activeJob;
    mutable std::mutex analysisMutex;
    std::unordered_map<std::wstring, AnalysisResult> completedAnalyses;
    std::unordered_map<std::wstring, PlacementPlan> placementPlans;
    std::unordered_map<std::wstring, MovePlan> movePlans;
    std::unordered_map<std::wstring, MoveExecutionResult> executionResults;
    mutable std::mutex profileMutex;
    std::vector<OptimizationProfile> profiles;
    OptimizationProfile activeProfile;
};

} // namespace icd
