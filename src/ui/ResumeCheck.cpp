#include "ui/ResumeCheck.h"

#include "core/exec/Executor.h"
#include "core/log/Log.h"

#include <wx/event.h>

#include <exception>
#include <utility>

namespace icd::ui {

    ResumeCheck::ResumeCheck(wxEvtHandler& target, core::Executor& executor, Handler handler) :
        events_(target), executor_(executor), handler_(std::move(handler)), mailbox_(std::make_shared<Mailbox>()) {
        mailbox_->target = this;
    }

    ResumeCheck::~ResumeCheck() {
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void ResumeCheck::look(const std::string& volumeName) {
        const std::uint64_t generation = ++generation_;
        executor_.submit([mailbox = mailbox_, events = &events_, volumeName, generation] {
            std::optional<services::UnfinishedRun> run;
            try {
                run = services::findUnfinishedRun(services::executionLogDirectory(), volumeName);
            }
            catch (const std::exception& error) {
                core::logging::warning("unfinished run not looked up", {{"what", error.what()}});
            }
            const std::scoped_lock lock(mailbox->mutex);
            if (mailbox->target != nullptr) {
                events->CallAfter([mailbox, generation, run = std::move(run)]() mutable {
                    if (ResumeCheck* target = mailbox->target; target != nullptr) {
                        target->deliver(generation, std::move(run));
                    }
                });
            }
        });
    }

    void ResumeCheck::forget() { ++generation_; }

    void ResumeCheck::deliver(std::uint64_t generation, std::optional<services::UnfinishedRun> run) {
        if (generation == generation_) {
            handler_(std::move(run));
        }
    }

} // namespace icd::ui
