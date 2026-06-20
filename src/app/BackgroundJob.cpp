#include "BackgroundJob.h"

#include <utility>

namespace icd {

BackgroundJob::~BackgroundJob()
{
    RequestCancel();
    Join();
}

bool BackgroundJob::Start(Work work)
{
    if (running.exchange(true)) {
        return false;
    }

    if (worker.joinable()) {
        worker.join();
    }

    cancellationRequested.store(false);
    worker = std::thread([this, work = std::move(work)]() mutable {
        try {
            work(cancellationRequested);
        } catch (...) {
        }

        running.store(false);
    });

    return true;
}

void BackgroundJob::RequestCancel()
{
    cancellationRequested.store(true);
}

void BackgroundJob::Join()
{
    if (worker.joinable()) {
        worker.join();
    }
}

} // namespace icd
