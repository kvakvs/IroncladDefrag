#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace icd {

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
