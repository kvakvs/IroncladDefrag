#pragma once

#include <functional>
#include <mutex>
#include <string>

#include "../analysis/FakeAnalysisService.h"
#include "../model/DomainTypes.h"
#include "BackgroundJob.h"

namespace icd {

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

    bool StartFakeAnalysis();
    void CancelActiveJob();
    bool IsJobRunning() const;

private:
    void NotifyProgress(const JobProgress& progress);
    void NotifyCompletion(const AnalysisResult& result);
    void NotifyError(const std::wstring& message);

    mutable std::mutex callbackMutex;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    ErrorCallback errorCallback;
    BackgroundJob activeJob;
};

} // namespace icd
