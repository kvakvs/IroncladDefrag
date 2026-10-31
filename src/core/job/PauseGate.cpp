#include "core/job/PauseGate.h"

namespace icd::core {

    void PauseGate::pause() {
        const std::scoped_lock lock(mutex_);
        paused_ = true;
    }

    void PauseGate::resume() {
        {
            const std::scoped_lock lock(mutex_);
            paused_ = false;
        }
        changed_.notify_all();
    }

    bool PauseGate::paused() const {
        const std::scoped_lock lock(mutex_);
        return paused_;
    }

    void PauseGate::wait(const CancelToken& token) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, token.stdToken(), [this] { return !paused_; });
    }

} // namespace icd::core
