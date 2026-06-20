#pragma once

#include <functional>
#include <mutex>
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
    using ErrorCallback = std::function<void(const std::wstring&)>;

    ApplicationController() = default;
    ~ApplicationController();

    ApplicationController(const ApplicationController&) = delete;
    ApplicationController& operator=(const ApplicationController&) = delete;

    void SetProgressCallback(ProgressCallback callback);
    void SetCompletionCallback(CompletionCallback callback);
    void SetErrorCallback(ErrorCallback callback);
    void ClearCallbacks();

    std::vector<DriveInfo> EnumerateDrives() const;
    bool StartFakeAnalysis();
    bool StartDriveAnalysis(const DriveInfo& drive);
    void RequestCancelActiveJob();
    void CancelActiveJob();
    bool IsJobRunning() const;
    std::vector<AnalysisResult> GetAnalysisSnapshots() const;

private:
    void NotifyProgress(const JobProgress& progress);
    void NotifyCompletion(const AnalysisResult& result);
    void NotifyError(const std::wstring& message);

    mutable std::mutex callbackMutex;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    ErrorCallback errorCallback;
    BackgroundJob activeJob;
    mutable std::mutex analysisMutex;
    std::unordered_map<std::wstring, AnalysisResult> completedAnalyses;
};

} // namespace icd
