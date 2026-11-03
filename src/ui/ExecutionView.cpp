#include "ui/ExecutionView.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/Duration.h"
#include "ui/panels/BottomPanel.h"
#include "ui/tabs/ProgressTab.h"

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

        // Time left from the bytes still to move at the recent rate; empty while the rate is unknown.
        wxString etaText(const services::ExecutionCounters& c, std::optional<double> rate) {
            if (!rate || *rate <= 0.0 || c.bytesPending < c.bytesMoved) {
                return {};
            }
            const double seconds = static_cast<double>(c.bytesPending - c.bytesMoved) / *rate;
            return wxString::FromUTF8(core::formatDuration(std::chrono::seconds(std::llround(seconds))));
        }

        // Share of the run done: by bytes, or by moves when nothing is left to move (resumed, already done).
        double doneFraction(const services::ExecutionCounters& c) {
            if (c.bytesPending != 0) {
                return std::min(1.0, static_cast<double>(c.bytesMoved) / static_cast<double>(c.bytesPending));
            }
            return c.moves != 0 ? static_cast<double>(c.finished) / static_cast<double>(c.moves) : 0.0;
        }

    } // namespace

    ExecutionView::ExecutionView(BottomPanel& bottom) : bottom_(bottom) {}

    services::JobFunction ExecutionView::start(services::ExecuteJobRequest request,
                                               std::shared_ptr<core::PauseGate> pause) {
        feed_ = std::make_shared<services::ExecutionFeed>();
        throughput_.reset();
        return services::makeExecuteJob(std::move(request), feed_, std::move(pause));
    }

    void ExecutionView::fill(ProgressDetails& details, std::optional<double>& fraction,
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
        details.eta = etaText(c, rate);
        details.currentFile = wxString::FromUTF8(c.currentFile);
        details.moves = wxString::FromUTF8(movesText(c));
        fraction = doneFraction(c);
    }

    std::optional<std::string> ExecutionView::finish(std::string_view drive) {
        if (!feed_) {
            return std::nullopt;
        }
        takeLines();
        const std::optional<services::ExecutionSummary> summary = feed_->summary();
        feed_.reset();
        if (!summary) {
            return std::nullopt;
        }
        for (const services::ExecutionLogLine& line : services::summaryLines(*summary)) {
            bottom_.addLogEntry(wxString::FromUTF8(line.file), wxString::FromUTF8(line.result),
                                wxString::FromUTF8(line.details));
        }
        return services::summaryStatus(*summary, drive);
    }

    void ExecutionView::takeLines() {
        for (const services::ExecutionLogLine& line : feed_->takeLines()) {
            bottom_.addLogEntry(wxString::FromUTF8(line.file), wxString::FromUTF8(line.result),
                                wxString::FromUTF8(line.details));
        }
    }

} // namespace icd::ui
