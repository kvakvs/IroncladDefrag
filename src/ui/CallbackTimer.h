#pragma once

#include <wx/timer.h>

#include <functional>
#include <utility>

namespace icd::ui {

    // A wxTimer that calls a function, for helpers that are not windows (the live map, the execution view) and so
    // have no event handler of their own to receive timer events.
    class CallbackTimer : public wxTimer {
    public:
        // Calls tick on every notification.
        explicit CallbackTimer(std::function<void()> tick) : tick_(std::move(tick)) {}

        // Calls the callback.
        void Notify() override { tick_(); }

    private:
        std::function<void()> tick_; // What each tick does.
    };

} // namespace icd::ui
