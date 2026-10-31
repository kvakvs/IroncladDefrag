#include "ui/bridge/JobEvents.h"

#include <utility>

namespace icd::ui {

    const wxEventTypeTag<wxThreadEvent>& jobProgressEvent() {
        static const wxEventTypeTag<wxThreadEvent> type(wxNewEventType());
        return type;
    }

    const wxEventTypeTag<wxThreadEvent>& jobFinishedEvent() {
        static const wxEventTypeTag<wxThreadEvent> type(wxNewEventType());
        return type;
    }

    void JobEventBridge::detach() {
        const std::scoped_lock lock(mutex_);
        target_ = nullptr;
    }

    void JobEventBridge::onProgress(services::JobId id, const core::Progress& progress) {
        auto event = std::make_unique<wxThreadEvent>(jobProgressEvent());
        event->SetPayload(JobProgressPayload{id, progress});
        post(std::move(event));
    }

    void JobEventBridge::onFinished(services::JobId id, const services::JobResult& result) {
        auto event = std::make_unique<wxThreadEvent>(jobFinishedEvent());
        event->SetPayload(JobFinishedPayload{id, result});
        post(std::move(event));
    }

    void JobEventBridge::post(std::unique_ptr<wxThreadEvent> event) {
        const std::scoped_lock lock(mutex_);
        if (target_ != nullptr) {
            wxQueueEvent(target_, event.release()); // wx takes ownership.
        }
    }

} // namespace icd::ui
