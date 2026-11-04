#include "ui/map/Legend.h"

#include "core/exec/Executor.h"
#include "core/format/ByteSize.h"
#include "core/log/Log.h"
#include "core/map/MapData.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>

#include <format>
#include <mutex>
#include <string>
#include <utility>

namespace icd::ui {

    // Where count workers send their results: the legend while it lives, nobody after it is destroyed. CallAfter
    // only queues an event, which a destroyed window drops (as for the map's tiles).
    struct LegendMailbox {
        std::mutex mutex; // Guards target.
        Legend* target = nullptr; // Receiver; null after the legend is gone.
    };

    namespace {

        // How long the view must rest before the legend counts it; panning and zooming never wait for counts.
        constexpr int restMilliseconds = 150;

        // Hands finished counts to the legend, if it still exists.
        void post(const std::shared_ptr<LegendMailbox>& mailbox, std::uint64_t request, core::LegendCounts counts) {
            const std::scoped_lock lock(mailbox->mutex);
            if (mailbox->target == nullptr) {
                return;
            }
            Legend* legend = mailbox->target;
            legend->CallAfter([legend, request, counts = std::move(counts)]() mutable {
                legend->deliver(request, std::move(counts));
            });
        }

        // A map color as a wx color.
        wxColour colourOf(core::Rgba color) {
            return {static_cast<unsigned char>((color >> 16U) & 0xFFU),
                    static_cast<unsigned char>((color >> 8U) & 0xFFU), static_cast<unsigned char>(color & 0xFFU)};
        }

        // An entry's text with its count: "Fragmented 1.2 GB (3.4%)".
        std::string countText(std::string_view label, core::ClusterCount clusters, const core::LegendCounts& counts,
                              std::uint64_t bytesPerCluster) {
            const double percent =
                counts.total == 0 ? 0.0 : 100.0 * static_cast<double>(clusters) / static_cast<double>(counts.total);
            return std::format("{} {} ({:.1f}%)", label, core::formatBytes(clusters * bytesPerCluster), percent);
        }

    } // namespace

    Legend::Legend(wxWindow* parent, core::Executor& executor) :
        wxScrolledWindow(parent), executor_(executor), mailbox_(std::make_shared<LegendMailbox>()), restTimer_(this) {
        mailbox_->target = this;
        sizer_ = new wxWrapSizer(wxHORIZONTAL);
        SetSizer(sizer_);
        SetScrollRate(0, FromDIP(10)); // Vertical only: entries wrap to the width.
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            CallAfter([this] { relayout(); }); // After the new size is in place.
        });
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { startCount(); }, restTimer_.GetId());
        rebuild();
    }

    Legend::~Legend() {
        restTimer_.Stop();
        cancel_.cancel();
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void Legend::setMap(std::shared_ptr<const core::MapData> map) {
        map_ = std::move(map);
        view_.reset();
        counts_.reset();
        cancel_.cancel();
        ++request_;
        showCounts();
    }

    void Legend::updateMap(std::shared_ptr<const core::MapData> map) {
        if (map_ == nullptr || map == nullptr || map->world().clusters() != map_->world().clusters()) {
            setMap(std::move(map));
            return;
        }
        map_ = std::move(map); // View and counts stay: the old numbers show until the recount replaces them.
        if (view_) {
            restTimer_.StartOnce(restMilliseconds);
        }
    }

    void Legend::setMode(core::ColorMode mode) {
        if (mode == mode_) {
            return;
        }
        mode_ = mode;
        counts_.reset();
        rebuild();
        startCount();
    }

    void Legend::setView(const core::Viewport& view) {
        view_ = view;
        restTimer_.StartOnce(restMilliseconds);
    }

    void Legend::rebuild() {
        sizer_->Clear(true);
        entries_.clear();
        const int square = FromDIP(12);
        for (const core::LegendEntry& entry : core::legendOf(mode_)) {
            auto* swatch = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(square, square), wxBORDER_SIMPLE);
            swatch->SetBackgroundColour(colourOf(entry.color));
            auto* text = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(std::string(entry.label)));
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(swatch, wxSizerFlags().CenterVertical());
            row->Add(text, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(4)));
            sizer_->Add(row, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6)));
            entries_.push_back({swatch, text});
        }
        showCounts();
    }

    void Legend::startCount() {
        restTimer_.Stop();
        cancel_.cancel();
        cancel_ = core::CancelSource();
        ++request_;
        if (map_ == nullptr || !view_) {
            return;
        }
        counts_.reset();
        const core::CellRect cells = core::visibleCells(map_->world(), *view_);
        executor_.submit([map = map_, mode = mode_, cells, mailbox = mailbox_, token = cancel_.token(),
                          request = request_, &executor = executor_] {
            try {
                post(mailbox, request,
                     core::countLegend(map->world(), map->source().facts, mode, cells, executor, token));
            }
            catch (const core::OperationCancelled&) {
                core::logging::debug("legend count cancelled"); // A newer view or mode replaced it.
            }
            catch (const std::exception& error) {
                core::logging::error("legend count failed", {{"what", error.what()}});
            }
        });
    }

    void Legend::deliver(std::uint64_t request, core::LegendCounts counts) {
        if (request != request_) {
            return; // Counted for an older view, mode or map.
        }
        counts_ = std::move(counts);
        showCounts();
    }

    void Legend::showCounts() {
        const std::span<const core::LegendEntry> legend = core::legendOf(mode_);
        const bool counted = counts_ && map_ != nullptr && counts_->clusters.size() == entries_.size();
        for (std::size_t index = 0; index < entries_.size() && index < legend.size(); ++index) {
            const std::string text = counted ? countText(legend[index].label, counts_->clusters[index], *counts_,
                                                         map_->snapshot().geometry.bytesPerCluster)
                                             : std::string(legend[index].label);
            entries_[index].text->SetLabelText(wxString::FromUTF8(text));
        }
        // The texts changed width, so the entries may wrap differently.
        relayout();
    }

    void Legend::relayout() {
        const wxSize client = GetClientSize();
        sizer_->InformFirstDirection(wxHORIZONTAL, client.x, client.y); // Wrap to the visible width.
        SetVirtualSize(client.x, sizer_->CalcMin().y);
        Layout();
    }

} // namespace icd::ui
