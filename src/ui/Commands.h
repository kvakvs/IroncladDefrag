#pragma once

#include <wx/defs.h>

#include <cstdint>
#include <span>

class wxFrame;

namespace icd::ui {

    // Command ids shared by menus, bottom-panel buttons and accelerators, so one handler and one enable rule
    // serve every way of triggering a command. Values are stable: scripted checks post them as WM_COMMAND
    // (wxID_HIGHEST is 6000). Standard commands (Exit, About, Preferences) use wx's own ids.
    enum CommandId : std::uint16_t {
        ID_CANCEL = wxID_HIGHEST + 1, // Optimize > Cancel: stops the running job (6001).
        ID_ANALYZE, // Drive > Analyze (6002).
        ID_REFRESH_DRIVES, // Drive > Refresh drive list.
        ID_TRIM, // Drive > TRIM (SSDs only).

        ID_QUICK, // Optimize > Quick: analyze and a Fast plan, stopping at review.
        ID_CHOOSE_STRATEGY, // Optimize > Choose strategy (opens the Strategy tab).
        ID_BUILD_PLAN, // Optimize > Build plan.
        ID_REVIEW_PLAN, // Optimize > Review plan on map.
        ID_EXECUTE, // Optimize > Execute plan.
        ID_PAUSE, // Optimize > Pause / Resume execution.

        ID_COLOR_CLASS, // View > Color mode > File class.
        ID_COLOR_FRAGMENTATION, // View > Color mode > Fragmentation.
        ID_COLOR_AGE, // View > Color mode > Age / heat.
        ID_COLOR_SIZE, // View > Color mode > Size.
        ID_COLOR_ZONE_FIT, // View > Color mode > Zone fit.
        ID_COLOR_PLAN, // View > Color mode > Plan preview.
        ID_SHOW_MINIMAP, // View > Minimap.
        ID_SHOW_LEGEND, // View > Legend.
        ID_ZOOM_IN, // View > Zoom in.
        ID_ZOOM_OUT, // View > Zoom out.
        ID_ZOOM_FIT, // View > Fit volume.
        ID_ZOOM_CLUSTER, // View > One cluster per pixel.

        ID_RESOURCES, // Settings > Resources (thread and memory caps).

        // Map commands added after the first set; appended so the ids above keep their values.
        ID_GOTO_FILE, // View > Go to file: search the drive and jump to a file.
        ID_NEXT_FRAGMENT, // View > Next fragment of the selected file.
        ID_PREVIOUS_FRAGMENT, // View > Previous fragment of the selected file.
        ID_SHORTCUTS, // Help > Keyboard shortcuts: lists every menu shortcut and map key.

        // Map context menu (right click on a file); handled by the map panel, not in the menu bar.
        ID_SHOW_IN_EXPLORER, // Opens the file's folder in Explorer with the file selected.
        ID_COPY_PATH, // Copies the file's full path to the clipboard.

        // Drive > Windows schedule: takes the drive out of Windows' scheduled optimization or puts it back (T120).
        ID_WINDOWS_SCHEDULE,
    };

    // A workflow command as the bottom panel's buttons and the drive list's context menu show it, so both list the
    // same steps in the same order with the same names.
    struct WorkflowCommand {
        int id = 0; // Command id.
        const char* label = ""; // Text with & mnemonic.
        bool startsGroup = false; // A separator goes before it in a menu (job control after the steps).
    };

    // The workflow steps in order (Analyze, Quick, Build plan, Execute...), then job control (Pause, Cancel).
    [[nodiscard]] std::span<const WorkflowCommand> workflowCommands();

    // Whether a command acts on the drive map (View menu: color modes, overlays, zoom, selection), and so needs a map
    // to be enabled.
    [[nodiscard]] constexpr bool isMapCommand(int id) {
        return (id >= ID_COLOR_CLASS && id <= ID_ZOOM_CLUSTER) || (id >= ID_GOTO_FILE && id <= ID_PREVIOUS_FRAGMENT);
    }

    // Enables or disables a command everywhere it appears in frame: its menu item and any button with the same
    // id. Called explicitly on state changes: wxMSW refreshes wxEVT_UPDATE_UI for menus only when a menu opens,
    // and ignores accelerators of disabled items.
    void enableCommand(wxFrame& frame, int id, bool enabled);

} // namespace icd::ui
