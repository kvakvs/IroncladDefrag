#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace icd {

// Runs one cancellable worker thread so long drive operations stay off the UI thread.
class BackgroundJob {
public:
    using Work = std::function<void(const std::atomic_bool& cancellationRequested)>;

    BackgroundJob() = default;
    ~BackgroundJob();

    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;

    bool Start(Work work);
    void RequestCancel();
    void Join();

    bool IsRunning() const { return running.load(); }
    bool IsCancellationRequested() const { return cancellationRequested.load(); }

private:
    std::atomic_bool running = false;
    std::atomic_bool cancellationRequested = false;
    std::thread worker;
};

} // namespace icd
