#pragma once

#include "services/execute/ExecutionLog.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

class wxEvtHandler;

namespace icd::core {
    class Executor;
}

namespace icd::ui {

    // Looks for a volume's unfinished run off the UI thread (T112): the newest execution log is read in full to count
    // what is left, and a long run's log is tens of MB. Only the latest look reports; an earlier one still reading is
    // dropped when it ends. UI thread only.
    class ResumeCheck {
    public:
        // What a look found (nullopt: nothing to resume); called on the UI thread.
        using Handler = std::function<void(std::optional<services::UnfinishedRun>)>;

        // Reads on executor (which must outlive this object) and reports through target's event queue.
        ResumeCheck(wxEvtHandler& target, core::Executor& executor, Handler handler);

        // Drops a look still reading.
        ~ResumeCheck();

        ResumeCheck(const ResumeCheck&) = delete;
        ResumeCheck& operator=(const ResumeCheck&) = delete;

        // Starts a look at a volume's logs in the default log directory.
        void look(const std::string& volumeName);

        // Drops a look under way (another drive was selected, or one with no logs to look at).
        void forget();

    private:
        // Where a finished look goes; cleared when the object goes away.
        struct Mailbox {
            std::mutex mutex; // Guards target.
            ResumeCheck* target = nullptr; // The live object, or null.
        };

        // Takes a finished look on the UI thread, unless a later one started.
        void deliver(std::uint64_t generation, std::optional<services::UnfinishedRun> run);

        wxEvtHandler& events_; // Whose queue results go through.
        core::Executor& executor_; // Reads the logs.
        Handler handler_; // Gets the results.
        std::shared_ptr<Mailbox> mailbox_; // Shared with looks under way.
        std::uint64_t generation_ = 0; // Bumped per look and per forget(), so only the latest look reports.
    };

} // namespace icd::ui
