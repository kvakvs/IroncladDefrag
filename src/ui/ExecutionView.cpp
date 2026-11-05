#include "ui/ExecutionView.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/Duration.h"
#include "ui/panels/BottomPanel.h"
#include "ui/tabs/LogTab.h"
#include "ui/tabs/ProgressTab.h"

#include <wx/wupdlock.h>

#include <cmath>
#include <format>
#include <utility>

namespace icd::ui {

    namespace {

        // "120 of 400 moves ended: 117 done, 2 skipped, 1 partly moved; 3 dropped before the start".
        std::string movesText(const services::ExecutionCounters& c) {
            std::string text =
                std::format("{} of {} moves ended: {} done, {} skipped, {} partly moved", core::formatCount(c.finished),
                            core::formatCount(c.moves), core::formatCount(c.moved), core::formatCount(c.skipped),
                            core::formatCount(c.partial));
            if (c.dropped != 0) {
                text += std::format("; {} dropped before the start", core::formatCount(c.dropped));
            }
            return text;
        }

        // How often new Log rows are taken from the feed.
        constexpr int rowsMs = 250;

        // Time left from the bytes still to move at the recent rate; nullopt while the rate is unknown.
        std::optional<std::chrono::seconds> timeLeft(const services::ExecutionCounters& c, std::optional<double> rate) {
            if (!rate || *rate <= 0.0 || c.bytesPending < c.bytesMoved) {
                return std::nullopt;
            }
            const double seconds = static_cast<double>(c.bytesPending - c.bytesMoved) / *rate;
            return std::chrono::seconds(std::llround(seconds));
        }

        // A feed line's time for the Log tab.
        wxDateTime whenOf(const services::ExecutionLogLine& line) {
            if (line.at == std::chrono::system_clock::time_point{}) {
                return wxDateTime::Now();
            }
            return wxDateTime(static_cast<time_t>(std::chrono::system_clock::to_time_t(line.at)));
        }

        // Share of the run done: by bytes, or by moves when nothing is left to move (resumed, already done).
        double doneFraction(const services::ExecutionCounters& c) {
            if (c.bytesPending != 0) {
                return std::min(1.0, static_cast<double>(c.bytesMoved) / static_cast<double>(c.bytesPending));
            }
            return c.moves != 0 ? static_cast<double>(c.finished) / static_cast<double>(c.moves) : 0.0;
        }

        // What the status line calls a finished run of a kind.
        const char* runWords(RunKind kind) {
            switch (kind) {
            case RunKind::File:
                return "File defragmented";
            case RunKind::Resume:
                return "Resumed run completed";
            case RunKind::Plan:
                break;
            }
            return "Plan executed";
        }

    } // namespace

    ExecutionView::ExecutionView(BottomPanel& bottom) :
        bottom_(bottom), rows_([this] {
            if (feed_) {
                takeLines();
            }
        }) {}

    ExecutionView::~ExecutionView() { rows_.Stop(); }

    std::shared_ptr<services::ExecutionFeed> ExecutionView::follow(RunKind kind) {
        feed_ = std::make_shared<services::ExecutionFeed>();
        kind_ = kind;
        throughput_.reset();
        rows_.Start(rowsMs);
        return feed_;
    }

    services::JobFunction ExecutionView::start(services::ExecuteJobRequest request,
                                               std::shared_ptr<core::PauseGate> pause) {
        return services::makeExecuteJob(std::move(request), follow(RunKind::Plan), std::move(pause));
    }

    services::JobFunction ExecutionView::startFile(services::FileDefragRequest request,
                                                   std::shared_ptr<core::PauseGate> pause) {
        return services::makeFileDefragJob(std::move(request), follow(RunKind::File), std::move(pause));
    }

    services::JobFunction ExecutionView::startResume(services::ResumeJobRequest request,
                                                     std::shared_ptr<core::PauseGate> pause) {
        return services::makeResumeJob(std::move(request), follow(RunKind::Resume), std::move(pause));
    }

    void ExecutionView::fill(ProgressDetails& details, std::optional<double>& fraction,
                             std::optional<std::chrono::seconds>& remaining,
                             std::chrono::steady_clock::time_point now) {
        if (!feed_) {
            return;
        }
        takeLines();
        const services::ExecutionCounters c = feed_->counters();
        if (!c.moving) {
            return; // Still reading the volume again: the analysis stages say what happens.
        }
        throughput_.add(now, static_cast<double>(c.bytesMoved));
        const std::optional<double> rate = throughput_.perSecond();
        details.done = wxString::FromUTF8(
            std::format("{} of {}", core::formatBytes(c.bytesMoved), core::formatBytes(c.bytesPending)));
        details.throughput =
            rate ? wxString::FromUTF8(core::formatBytes(static_cast<std::uint64_t>(*rate)) + "/s") : wxString();
        remaining = timeLeft(c, rate);
        details.currentFile = wxString::FromUTF8(c.currentFile);
        details.moves = wxString::FromUTF8(movesText(c));
        fraction = doneFraction(c);
    }

    std::optional<std::string> ExecutionView::finish(std::string_view drive) {
        if (!feed_) {
            return std::nullopt;
        }
        rows_.Stop();
        takeLines();
        const std::optional<services::ExecutionSummary> summary = feed_->summary();
        feed_.reset();
        if (!summary) {
            return std::nullopt;
        }
        for (const services::ExecutionLogLine& line : services::summaryLines(*summary)) {
            bottom_.log().add(wxString::FromUTF8(line.file), wxString::FromUTF8(line.result),
                              wxString::FromUTF8(line.details), whenOf(line));
        }
        return services::summaryStatus(*summary, drive, runWords(kind_));
    }

    void ExecutionView::takeLines() {
        const std::vector<services::ExecutionLogLine> lines = feed_->takeLines();
        if (lines.empty()) {
            return;
        }
        const wxWindowUpdateLocker frozen(&bottom_.log()); // One repaint for a burst of rows.
        for (const services::ExecutionLogLine& line : lines) {
            bottom_.log().add(wxString::FromUTF8(line.file), wxString::FromUTF8(line.result),
                              wxString::FromUTF8(line.details), whenOf(line));
        }
    }

} // namespace icd::ui
