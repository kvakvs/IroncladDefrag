#pragma once

#include "core/job/Progress.h"
#include "services/job/JobRunner.h"

#include <wx/event.h>

#include <memory>
#include <mutex>

namespace icd::ui {

    // Data of a jobProgressEvent() (wxThreadEvent payload).
    struct JobProgressPayload {
        services::JobId id = 0; // Job the progress belongs to.
        core::Progress progress; // The snapshot.
    };

    // Data of a jobFinishedEvent() (wxThreadEvent payload).
    struct JobFinishedPayload {
        services::JobId id = 0; // Job that ended.
        services::JobResult result; // How it ended.
    };

    // Event type for throttled job progress, delivered on the UI thread. Created on first use instead of as a
    // static object (wxDEFINE_EVENT), so its construction cannot throw before main().
    const wxEventTypeTag<wxThreadEvent>& jobProgressEvent();

    // Event type for the end of a job, delivered once on the UI thread. Created on first use, as above.
    const wxEventTypeTag<wxThreadEvent>& jobFinishedEvent();

    // Turns job callbacks from worker threads into wx events queued to one window (wxQueueEvent is the
    // thread-safe way into the UI thread). The window calls detach() before it is destroyed; events from jobs
    // that are still running are then dropped instead of reaching a deleted window.
    class JobEventBridge : public services::JobObserver {
    public:
        // Events go to target until detach().
        explicit JobEventBridge(wxEvtHandler* target) : target_(target) {}

        // Stops delivery; call on the UI thread before target is destroyed.
        void detach();

        // Queues a jobProgressEvent().
        void onProgress(services::JobId id, const core::Progress& progress) override;

        // Queues a jobFinishedEvent().
        void onFinished(services::JobId id, const services::JobResult& result) override;

    private:
        // Queues an event to the target, or drops it when detached.
        void post(std::unique_ptr<wxThreadEvent> event);

        std::mutex mutex_; // Guards target_ against detach() racing a worker.
        wxEvtHandler* target_ = nullptr; // Receiver; null after detach().
    };

} // namespace icd::ui
