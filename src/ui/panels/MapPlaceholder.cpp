#include "ui/panels/MapPlaceholder.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace icd::ui {

    namespace {

        // What a placeholder cell shows; the real map colors come with the color modes in Phase 2.
        enum class FakeCell : std::uint8_t { Free, Used, Fragmented, System };

        // Fill colors indexed by FakeCell, as 0xRRGGBB (plain data: a static wxColour constructor may throw).
        constexpr std::array<std::uint32_t, 4> cellColours{0xECECEC, 0x4678C8, 0xD2463C, 0x3CA05A};

        // A cell color as a wxColour.
        wxColour cellColour(FakeCell kind) {
            const std::uint32_t rgb = cellColours.at(static_cast<std::size_t>(kind));
            return {static_cast<unsigned char>(rgb >> 16U), static_cast<unsigned char>(rgb >> 8U),
                    static_cast<unsigned char>(rgb)};
        }

        // Integer hash (lowbias32), so the pattern is stable for a seed without keeping state.
        std::uint32_t mix(std::uint32_t x) {
            x ^= x >> 16U;
            x *= 0x7feb352dU;
            x ^= x >> 15U;
            x *= 0x846ca68bU;
            x ^= x >> 16U;
            return x;
        }

        // Category of cell index out of count: more used space near the start of the volume, a block of
        // metadata a third of the way in, and some fragmented files among the used ones.
        FakeCell fakeCell(std::uint32_t seed, std::size_t index, std::size_t count) {
            const double position = static_cast<double>(index) / static_cast<double>(count);
            if (position > 0.33 && position < 0.345) {
                return FakeCell::System;
            }
            const auto hash = mix(seed * 2654435761U + static_cast<std::uint32_t>(index));
            const double draw = static_cast<double>(hash & 0xFFFFU) / 65536.0;
            if (draw >= 0.9 - 0.7 * position) {
                return FakeCell::Free;
            }
            return (hash >> 16U) % 9U == 0 ? FakeCell::Fragmented : FakeCell::Used;
        }

        // Color mode names, shown in the (disabled) toolbar choice; the same list as View > Color mode.
        constexpr std::array colorModes{"File class", "Fragmentation", "Age / heat",
                                        "Size",       "Zone fit",      "Plan preview"};

    } // namespace

    MapPlaceholder::MapPlaceholder(wxWindow* parent) : wxPanel(parent) {
        canvas_ = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE);
        canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        canvas_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(canvas_);
            paintCanvas(dc, canvas_->GetClientSize());
        });

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(createToolbar(), wxSizerFlags().Expand().Border(wxALL, FromDIP(4)));
        sizer->Add(canvas_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
    }

    wxSizer* MapPlaceholder::createToolbar() {
        auto* bar = new wxBoxSizer(wxHORIZONTAL);
        auto* colorMode = new wxChoice(this, wxID_ANY);
        for (const char* mode : colorModes) {
            colorMode->Append(mode);
        }
        colorMode->SetSelection(0);
        auto* zones = new wxCheckBox(this, wxID_ANY, "Zones");
        auto* plan = new wxCheckBox(this, wxID_ANY, "Plan overlay");
        auto* search = new wxSearchCtrl(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(220, -1)));
        search->SetDescriptiveText("Find file");

        bar->Add(new wxStaticText(this, wxID_ANY, "Color:"), wxSizerFlags().CenterVertical());
        bar->Add(colorMode, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        bar->Add(zones, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(6)));
        bar->Add(plan, wxSizerFlags().CenterVertical());
        bar->AddStretchSpacer();
        bar->Add(search, wxSizerFlags().CenterVertical());
        for (wxWindow* control : {static_cast<wxWindow*>(colorMode), static_cast<wxWindow*>(zones),
                                  static_cast<wxWindow*>(plan), static_cast<wxWindow*>(search)}) {
            control->Disable(); // The map engine (Phase 2) brings these to life.
        }
        return bar;
    }

    void MapPlaceholder::setCaption(const wxString& caption) {
        caption_ = caption;
        canvas_->Refresh();
    }

    void MapPlaceholder::setPattern(std::uint32_t seed) {
        seed_ = seed;
        canvas_->Refresh();
    }

    void MapPlaceholder::paintCanvas(wxDC& dc, const wxSize& size) const {
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
        dc.Clear();
        const int cell = FromDIP(7);
        const int columns = std::max(1, (size.x - 1) / cell);
        const int rows = std::max(1, (size.y - 1) / cell);
        const auto count = static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
        dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
        const std::array brushes{wxBrush(cellColour(FakeCell::Free)), wxBrush(cellColour(FakeCell::Used)),
                                 wxBrush(cellColour(FakeCell::Fragmented)), wxBrush(cellColour(FakeCell::System))};
        for (std::size_t index = 0; index < count; ++index) {
            const FakeCell kind = seed_ == 0 ? FakeCell::Free : fakeCell(seed_, index, count);
            dc.SetBrush(brushes.at(static_cast<std::size_t>(kind)));
            const auto column = static_cast<int>(index % static_cast<std::size_t>(columns));
            const auto row = static_cast<int>(index / static_cast<std::size_t>(columns));
            dc.DrawRectangle(column * cell, row * cell, cell, cell);
        }
        if (!caption_.empty()) {
            const wxSize extent = dc.GetTextExtent(caption_);
            const int pad = FromDIP(8);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
            dc.DrawRectangle(pad, pad, extent.x + 2 * pad, extent.y + 2 * pad);
            dc.SetTextForeground(wxColour(32, 32, 32));
            dc.DrawText(caption_, 2 * pad, 2 * pad);
        }
    }

} // namespace icd::ui
