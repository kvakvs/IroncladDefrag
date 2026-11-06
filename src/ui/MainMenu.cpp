#include "ui/MainMenu.h"

#include "ui/Commands.h"

#include <wx/menu.h>

#include <array>

namespace icd::ui {

    namespace {

        // One menu entry; wxID_SEPARATOR makes a separator line.
        struct ItemSpec {
            int id = wxID_SEPARATOR; // Command id.
            const char* label = ""; // Menu text with & mnemonic and optional \t accelerator.
            wxItemKind kind = wxITEM_NORMAL; // Plain, check or radio item.
        };

        constexpr ItemSpec separator{};

        constexpr std::array fileItems{
            ItemSpec{wxID_EXIT, "E&xit\tAlt+F4"},
        };

        constexpr std::array driveItems{
            ItemSpec{ID_ANALYZE, "&Analyze"},
            ItemSpec{ID_REFRESH_DRIVES, "&Refresh drive list"},
            separator,
            ItemSpec{ID_TRIM, "&TRIM (SSD)"},
            separator,
            ItemSpec{ID_WINDOWS_SCHEDULE, "Windows &schedule..."},
        };

        constexpr std::array optimizeItems{
            ItemSpec{ID_QUICK, "&Quick optimize"},
            separator,
            ItemSpec{ID_CHOOSE_STRATEGY, "Choose &strategy..."},
            ItemSpec{ID_BUILD_PLAN, "&Build plan"},
            ItemSpec{ID_REVIEW_PLAN, "&Review plan on map"},
            ItemSpec{ID_EXECUTE, "&Execute plan..."},
            ItemSpec{ID_RESUME_RUN, "Res&ume interrupted run..."},
            separator,
            ItemSpec{ID_DEFRAG_FILE, "&Defragment selected file..."},
            separator,
            ItemSpec{ID_PAUSE, "&Pause", wxITEM_CHECK},
            ItemSpec{ID_CANCEL, "&Cancel"},
        };

        constexpr std::array colorModeItems{
            ItemSpec{ID_COLOR_CLASS, "File &class", wxITEM_RADIO},
            ItemSpec{ID_COLOR_FRAGMENTATION, "&Fragmentation", wxITEM_RADIO},
            ItemSpec{ID_COLOR_AGE, "&Age / heat", wxITEM_RADIO},
            ItemSpec{ID_COLOR_SIZE, "&Size", wxITEM_RADIO},
            ItemSpec{ID_COLOR_ZONE_FIT, "&Zone fit", wxITEM_RADIO},
            ItemSpec{ID_COLOR_PLAN, "&Plan preview", wxITEM_RADIO},
        };

        constexpr std::array viewItems{
            ItemSpec{ID_SHOW_MINIMAP, "&Minimap", wxITEM_CHECK},
            ItemSpec{ID_SHOW_ACTIVITY, "Read/write &activity", wxITEM_CHECK},
            ItemSpec{ID_SHOW_LEGEND, "Map &colors and legend"},
            separator,
            ItemSpec{ID_ZOOM_IN, "Zoom &in"},
            ItemSpec{ID_ZOOM_OUT, "Zoom &out"},
            ItemSpec{ID_ZOOM_FIT, "&Fit volume"},
            ItemSpec{ID_ZOOM_CLUSTER, "One cl&uster per pixel"},
            separator,
            ItemSpec{ID_GOTO_FILE, "&Go to file..."},
            ItemSpec{ID_NEXT_FRAGMENT, "&Next fragment"},
            ItemSpec{ID_PREVIOUS_FRAGMENT, "P&revious fragment"},
        };

        constexpr std::array settingsItems{
            ItemSpec{ID_RESOURCES, "&Resources..."},
            ItemSpec{wxID_PREFERENCES, "&Preferences..."},
        };

        constexpr std::array helpItems{
            ItemSpec{ID_SHORTCUTS, "&Keyboard shortcuts"},
            separator,
            ItemSpec{wxID_ABOUT, "&About Ironclad Defrag"},
        };

        constexpr std::array stateCommands{
            int{ID_ANALYZE},
            int{ID_REFRESH_DRIVES},
            int{ID_TRIM},
            int{ID_QUICK},
            int{ID_CHOOSE_STRATEGY},
            int{ID_BUILD_PLAN},
            int{ID_REVIEW_PLAN},
            int{ID_EXECUTE},
            int{ID_PAUSE},
            int{ID_CANCEL},
            int{ID_COLOR_CLASS},
            int{ID_COLOR_FRAGMENTATION},
            int{ID_COLOR_AGE},
            int{ID_COLOR_SIZE},
            int{ID_COLOR_ZONE_FIT},
            int{ID_COLOR_PLAN},
            int{ID_SHOW_MINIMAP},
            int{ID_SHOW_LEGEND},
            int{ID_ZOOM_IN},
            int{ID_ZOOM_OUT},
            int{ID_ZOOM_FIT},
            int{ID_ZOOM_CLUSTER},
            int{ID_RESOURCES},
            int{wxID_PREFERENCES},
            int{ID_GOTO_FILE},
            int{ID_NEXT_FRAGMENT},
            int{ID_PREVIOUS_FRAGMENT},
            int{ID_WINDOWS_SCHEDULE},
            int{ID_DEFRAG_FILE},
            int{ID_RESUME_RUN},
        };

        // Appends items to menu in order.
        void appendItems(wxMenu& menu, std::span<const ItemSpec> items) {
            for (const ItemSpec& item : items) {
                if (item.id == wxID_SEPARATOR) {
                    menu.AppendSeparator();
                }
                else {
                    menu.Append(item.id, wxString::FromUTF8(item.label), wxString(), item.kind);
                }
            }
        }

        // A new menu holding items; ownership passes to the menu bar it is appended to.
        wxMenu* makeMenu(std::span<const ItemSpec> items) {
            auto* menu = new wxMenu();
            appendItems(*menu, items);
            return menu;
        }

    } // namespace

    wxMenuBar* createMainMenuBar() {
        auto* viewMenu = new wxMenu();
        viewMenu->AppendSubMenu(makeMenu(colorModeItems), "&Color mode");
        viewMenu->AppendSeparator();
        appendItems(*viewMenu, viewItems);

        auto* bar = new wxMenuBar();
        bar->Append(makeMenu(fileItems), "&File");
        bar->Append(makeMenu(driveItems), "&Drive");
        bar->Append(makeMenu(optimizeItems), "&Optimize");
        bar->Append(viewMenu, "&View");
        bar->Append(makeMenu(settingsItems), "&Settings");
        bar->Append(makeMenu(helpItems), "&Help");
        bar->Check(ID_SHOW_MINIMAP, true); // The map starts with its minimap.
        bar->Check(ID_COLOR_FRAGMENTATION, true); // The map's first color mode.
        return bar;
    }

    std::span<const int> stateDependentCommands() { return stateCommands; }

} // namespace icd::ui
