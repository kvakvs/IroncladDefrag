#include "ui/panels/StrategyPage.h"

#include "core/classify/Catalog.h"
#include "core/strategy/Presets.h"
#include "services/settings/StrategyLibrary.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/grid.h>
#include <wx/radiobox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string_view>

namespace icd::ui {

    namespace {

        // The Maximum-order choice that keeps the zone's own order.
        constexpr const char* sameOrder = "(same)";

        // Columns of the zone table.
        enum Column : std::uint8_t { Name, Classes, Action, Anchor, Order, MaxOrder, FragmentOk, Gap, ColumnCount };

        // Column titles and widths (DIP).
        constexpr std::array<std::pair<const char*, int>, ColumnCount> columns{{
            {"Zone", 150},
            {"File classes", 230},
            {"Action", 110},
            {"Anchor", 100},
            {"Order", 110},
            {"Order at Maximum", 120},
            {"Fragments OK", 95},
            {"Gap after", 190},
        }};

        // Script names of every value of an enum with a key function, in declaration order.
        template <typename E, std::size_t N>
        void addKeys(wxArrayString& out, std::string_view (*key)(E)) {
            for (std::size_t i = 0; i < N; ++i) {
                out.Add(wxString::FromUTF8(std::string(key(static_cast<E>(i)))));
            }
        }

        // The choices of an enum column, in script spelling; "(same)" at Maximum means no override.
        wxArrayString choicesOf(Column column) {
            wxArrayString out;
            if (column == Action) {
                addKeys<core::strategy::ZoneAction, 5>(out, &core::strategy::actionKey);
            }
            else if (column == Anchor) {
                addKeys<core::strategy::Anchor, 3>(out, &core::strategy::anchorKey);
            }
            else {
                if (column == MaxOrder) {
                    out.Add(sameOrder);
                }
                addKeys<core::strategy::ZoneOrder, 8>(out, &core::strategy::orderKey);
            }
            return out;
        }

        // The text of a cell for a zone.
        std::string cellText(const core::strategy::Zone& z, int column) {
            const auto maxOrder = z.orderByLevel.at(static_cast<std::size_t>(core::strategy::Effort::Maximum));
            switch (column) {
            case Name:
                return z.name;
            case Classes:
                return core::strategy::idsText(z.select.classes);
            case Action:
                return std::string(core::strategy::actionKey(z.action));
            case Anchor:
                return std::string(core::strategy::anchorKey(z.anchor));
            case Order:
                return std::string(core::strategy::orderKey(z.order));
            case MaxOrder:
                return maxOrder ? std::string(core::strategy::orderKey(*maxOrder)) : sameOrder;
            case FragmentOk:
                return core::strategy::fragmentOkText(z.fragmentOk);
            default:
                return core::strategy::gapText(z.gapAfter);
            }
        }

        // Applies an enum or name cell; false when the text is not one of the choices.
        bool applyChoice(core::strategy::Zone& z, int column, const std::string& text) {
            if (column == Action) {
                const auto action = core::strategy::actionNamed(text);
                z.action = action.value_or(z.action);
                return action.has_value();
            }
            if (column == Anchor) {
                const auto anchor = core::strategy::anchorNamed(text);
                z.anchor = anchor.value_or(z.anchor);
                return anchor.has_value();
            }
            const auto order = core::strategy::orderNamed(text);
            if (column == Order) {
                z.order = order.value_or(z.order);
                return order.has_value();
            }
            auto& slot = z.orderByLevel.at(static_cast<std::size_t>(core::strategy::Effort::Maximum));
            slot = text == sameOrder ? std::nullopt : order;
            return text == sameOrder || order.has_value();
        }

        // The picker's label of a strategy: custom ones are marked, so a copy of a preset is told from the original.
        wxString pickerLabel(const core::strategy::ZoneScript& script) {
            const std::string prefix = services::isCustomStrategyId(script.id) ? "Custom: " : "";
            return wxString::FromUTF8(prefix + script.name);
        }

        // Applies a free-text cell; false when the text cannot be read.
        bool applyText(core::strategy::Zone& z, int column, const std::string& text) {
            switch (column) {
            case Name:
                z.name = text;
                return true;
            case Classes:
                z.select.classes = core::strategy::parseIdsText(text);
                return true;
            case FragmentOk:
                {
                    const auto ok = core::strategy::parseFragmentOkText(text);
                    z.fragmentOk = ok ? *ok : z.fragmentOk;
                    return ok.has_value();
                }
            default:
                {
                    const auto gap = core::strategy::parseGapText(text);
                    z.gapAfter = gap.value_or(z.gapAfter);
                    return gap.has_value();
                }
            }
        }

    } // namespace

    StrategyPage::StrategyPage(wxWindow* parent) : wxPanel(parent), strategies_(core::strategy::builtinPresets()) {
        preset_ = new wxChoice(this, wxID_ANY);
        for (const auto& strategy : strategies_) {
            preset_->Append(pickerLabel(strategy));
        }
        const std::array<wxString, 3> levels{"Fast", "Moderate", "Maximum"};
        effort_ = new wxRadioBox(this, wxID_ANY, "Effort", wxDefaultPosition, wxDefaultSize, levels.size(),
                                 levels.data(), 1, wxRA_SPECIFY_ROWS);
        description_ = new wxStaticText(this, wxID_ANY, "");
        zones_ = new wxGrid(this, wxID_ANY);
        zones_->CreateGrid(0, ColumnCount);
        zones_->HideRowLabels();
        for (int c = 0; c < ColumnCount; ++c) {
            zones_->SetColLabelValue(c, columns.at(static_cast<std::size_t>(c)).first);
            zones_->SetColSize(c, FromDIP(columns.at(static_cast<std::size_t>(c)).second));
        }
        problemsText_ = new wxStaticText(this, wxID_ANY, "");
        const bool dark = wxSystemSettings::GetAppearance().IsUsingDarkBackground();
        problemsText_->SetForegroundColour(dark ? wxColour(255, 120, 110) : wxColour(190, 30, 30)); // Readable red.

        auto* top = new wxBoxSizer(wxHORIZONTAL);
        top->Add(new wxStaticText(this, wxID_ANY, "Preset:"), wxSizerFlags().CenterVertical());
        top->Add(preset_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(8)));
        top->Add(effort_, wxSizerFlags().CenterVertical());
        top->Add(createButtons(), wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(16)));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(top, wxSizerFlags().Border(wxALL, FromDIP(8)));
        sizer->Add(description_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
        sizer->Add(zones_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, FromDIP(8)));
        sizer->Add(problemsText_, wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
        SetSizer(sizer);

        preset_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            loadPreset();
            onUserChange();
        });
        effort_->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) { onUserChange(); });
        zones_->Bind(wxEVT_GRID_CELL_CHANGED, &StrategyPage::onCellChanged, this);
        reset();
        setEditable(false);
    }

    wxSizer* StrategyPage::createButtons() {
        saveAs_ = new wxButton(this, wxID_ANY,
                               "Save as..."); // No mnemonic: its letters belong to the menus and workflow buttons.
        delete_ = new wxButton(this, wxID_ANY, "De&lete");
        import_ = new wxButton(this, wxID_ANY, "&Import...");
        export_ = new wxButton(this, wxID_ANY, "E&xport...");
        saveAs_->SetToolTip("Keep the shown strategy, with your edits, as a custom strategy");
        delete_->SetToolTip("Delete the selected custom strategy");
        import_->SetToolTip("Add a custom strategy from a zone script file (.json)");
        export_->SetToolTip("Write the shown strategy, with your edits, to a zone script file (.json)");
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        const std::array<std::pair<wxButton*, StrategyCommand>, 4> buttons{{
            {saveAs_, StrategyCommand::SaveAs},
            {delete_, StrategyCommand::Delete},
            {import_, StrategyCommand::Import},
            {export_, StrategyCommand::Export},
        }};
        for (const auto& [button, command] : buttons) {
            row->Add(button, wxSizerFlags().Border(wxRIGHT, FromDIP(4)));
            button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent&) {
                if (onCommand_) {
                    onCommand_(command);
                }
            });
        }
        return row;
    }

    void StrategyPage::setStrategies(std::vector<core::strategy::ZoneScript> strategies) {
        if (strategies.empty()) {
            strategies = core::strategy::builtinPresets(); // The picker is never empty.
        }
        strategies_ = std::move(strategies);
        preset_->Clear();
        for (const auto& strategy : strategies_) {
            preset_->Append(pickerLabel(strategy));
        }
        selectIndex(0, effort());
    }

    std::size_t StrategyPage::preset() const {
        const int selection = preset_->GetSelection();
        return selection == wxNOT_FOUND ? 0 : static_cast<std::size_t>(selection);
    }

    const std::string& StrategyPage::presetId() const { return strategies_.at(preset()).id; }

    bool StrategyPage::customSelected() const { return services::isCustomStrategyId(presetId()); }

    services::EffortLevel StrategyPage::effort() const {
        return static_cast<services::EffortLevel>(effort_->GetSelection());
    }

    void StrategyPage::reset() { selectIndex(0, services::EffortLevel::Fast); }

    void StrategyPage::select(std::string_view id, services::EffortLevel effort) {
        const auto found = std::ranges::find(strategies_, id, &core::strategy::ZoneScript::id);
        selectIndex(static_cast<std::size_t>(found - strategies_.begin()), effort);
    }

    void StrategyPage::selectIndex(std::size_t index, services::EffortLevel effort) {
        preset_->SetSelection(index < strategies_.size() ? static_cast<int>(index) : 0);
        effort_->SetSelection(static_cast<int>(effort));
        loadPreset();
    }

    void StrategyPage::setEditable(bool editable) {
        editable_ = editable;
        preset_->Enable(editable);
        effort_->Enable(editable);
        zones_->EnableEditing(editable);
        refreshButtons();
    }

    void StrategyPage::refreshButtons() {
        const bool usable = problems_.empty();
        saveAs_->Enable(editable_ && usable);
        delete_->Enable(editable_ && customSelected());
        import_->Enable(editable_);
        export_->Enable(usable); // Writes a file only; a broken script would not import again.
    }

    void StrategyPage::loadPreset() {
        script_ = strategies_.at(preset());
        cellProblems_.clear();
        description_->SetLabelText(wxString::FromUTF8(script_.description));
        description_->Wrap(FromDIP(900));
        showZones();
        validate();
        Layout();
    }

    void StrategyPage::showZones() {
        if (zones_->GetNumberRows() > 0) {
            zones_->DeleteRows(0, zones_->GetNumberRows());
        }
        zones_->AppendRows(static_cast<int>(script_.zones.size()));
        for (std::size_t row = 0; row < script_.zones.size(); ++row) {
            const auto r = static_cast<int>(row);
            for (int c = 0; c < ColumnCount; ++c) {
                zones_->SetCellValue(r, c, wxString::FromUTF8(cellText(script_.zones[row], c)));
            }
            for (const Column c : {Action, Anchor, Order, MaxOrder}) {
                zones_->SetCellEditor(r, c, new wxGridCellChoiceEditor(choicesOf(c)));
            }
        }
    }

    bool StrategyPage::applyCell(int row, int column) {
        auto& zone = script_.zones.at(static_cast<std::size_t>(row));
        const std::string text = zones_->GetCellValue(row, column).utf8_string();
        const bool choice = column == Action || column == Anchor || column == Order || column == MaxOrder;
        const bool ok = choice ? applyChoice(zone, column, text) : applyText(zone, column, text);
        if (!ok) {
            cellProblems_.push_back(std::format("zone {} \"{}\": cannot read \"{}\" in {}", row + 1, zone.name, text,
                                                columns.at(static_cast<std::size_t>(column)).first));
        }
        return ok;
    }

    void StrategyPage::validate() {
        problems_ = cellProblems_;
        for (const auto& issue : core::strategy::validateScript(script_, core::classify::builtinCatalog())) {
            problems_.push_back(core::strategy::describeIssue(issue, script_));
        }
        std::string text;
        for (const std::string& p : problems_) {
            text += (text.empty() ? "" : "\n") + p;
        }
        problemsText_->SetLabelText(wxString::FromUTF8(text));
        refreshButtons();
        Layout();
    }

    void StrategyPage::onCellChanged(wxGridEvent& event) {
        cellProblems_.clear(); // Only the latest unreadable cell is kept; the others were read when they changed.
        (void)applyCell(event.GetRow(), event.GetCol());
        validate();
        if (onChange_) {
            onChange_();
        }
    }

    void StrategyPage::onUserChange() {
        if (onChange_) {
            onChange_();
        }
    }

} // namespace icd::ui
