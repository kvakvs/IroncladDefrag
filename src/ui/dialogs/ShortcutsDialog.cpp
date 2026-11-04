#include "ui/dialogs/ShortcutsDialog.h"

#include "ui/Accelerators.h"
#include "ui/AppIcon.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/sizer.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace icd::ui {

    namespace {

        // Heading of the map keys.
        constexpr const char* mapHeading = "Map (while it has the focus)";

        // List columns.
        enum Column : std::uint8_t {
            KeyColumn, // The keys.
            ActionColumn, // What they do.
        };

        // Adds the shortcuts of one top-level menu under its heading; nothing when it has none.
        void addMenu(const wxMenuBar& bar, std::size_t index, std::vector<ShortcutLine>& lines) {
            const wxMenu* menu = bar.GetMenu(index);
            bool headed = false;
            for (const Shortcut& shortcut : menuShortcuts()) {
                if (menu->FindItem(shortcut.id) == nullptr) {
                    continue;
                }
                if (!headed) {
                    lines.push_back({.keys = {}, .action = bar.GetMenuLabelText(index)});
                    headed = true;
                }
                lines.push_back({.keys = shortcutText(shortcut), .action = wxString::FromUTF8(shortcut.description)});
            }
        }

        // Adds the map keys, keys of one action joined on one line.
        void addMapKeys(std::vector<ShortcutLine>& lines) {
            lines.push_back({.keys = {}, .action = mapHeading});
            const std::span<const MapKeyBinding> bindings = mapKeyBindings();
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                const MapKeyBinding& binding = bindings[index];
                const bool sameAction = index > 0 && bindings[index - 1].key == binding.key;
                if (sameAction) {
                    lines.back().keys += " / " + mapKeyText(binding.keyCode);
                    continue;
                }
                lines.push_back(
                    {.keys = mapKeyText(binding.keyCode), .action = wxString::FromUTF8(binding.description)});
            }
        }

    } // namespace

    std::vector<ShortcutLine> shortcutLines(const wxMenuBar& bar) {
        std::vector<ShortcutLine> lines;
        for (std::size_t index = 0; index < bar.GetMenuCount(); ++index) {
            addMenu(bar, index, lines);
        }
        addMapKeys(lines);
        return lines;
    }

    ShortcutsDialog::ShortcutsDialog(wxWindow* parent, const wxMenuBar& bar) :
        wxDialog(parent, wxID_ANY, "Keyboard shortcuts", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        SetIcons(appIcons());
        auto* list =
            new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(480, 520)), wxLC_REPORT | wxLC_SINGLE_SEL);
        list->AppendColumn("Keys", wxLIST_FORMAT_LEFT, FromDIP(140));
        list->AppendColumn("Action", wxLIST_FORMAT_LEFT, FromDIP(310));
        wxFont bold = list->GetFont();
        bold.MakeBold();
        long row = 0;
        for (const ShortcutLine& line : shortcutLines(bar)) {
            const bool heading = line.keys.empty();
            list->InsertItem(row, heading ? line.action : line.keys);
            if (heading) {
                list->SetItemFont(row, bold);
            }
            else {
                list->SetItem(row, ActionColumn, line.action);
            }
            ++row;
        }

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(list, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));
        sizer->Add(CreateButtonSizer(wxCLOSE),
                   wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
        SetSizerAndFit(sizer);
        SetEscapeId(wxID_CLOSE);
        CentreOnParent();
    }

} // namespace icd::ui
