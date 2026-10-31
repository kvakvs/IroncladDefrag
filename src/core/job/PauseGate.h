#pragma once

#include "core/job/CancelToken.h"

#include <condition_variable>
#include <mutex>

namespace icd::core {

    // Lets the UI pause a running job between work items (Optimize > Pause during execution). Workers call
    // wait() where stopping is safe; it blocks while paused and returns early on cancellation, so Cancel
    // still works on a paused job.
    class PauseGate {
    public:
        // Makes wait() block from now on.
        void pause();

        // Releases every waiting worker.
        void resume();

        // Whether the gate is closed.
        [[nodiscard]] bool paused() const;

        // Blocks while paused, until resume() or cancellation of token.
        void wait(const CancelToken& token);

    private:
        mutable std::mutex mutex_; // Guards paused_.
        std::condition_variable_any changed_; // Signalled by resume().
        bool paused_ = false; // Whether wait() blocks.
    };

} // namespace icd::core
