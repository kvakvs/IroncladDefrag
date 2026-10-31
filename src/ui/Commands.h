#pragma once

#include <wx/defs.h>

#include <cstdint>

class wxFrame;

namespace icd::ui {

    // Command ids shared by menus, bottom-panel buttons and accelerators, so one handler and one enable rule
    // serve every way of triggering a command. Values are stable: scripted checks post them as WM_COMMAND
    // (wxID_HIGHEST is 6000). Standard commands (Exit, About, Preferences) use wx's own ids.
    enum CommandId : std::uint16_t {
        ID_RUN_FAKE_JOB = wxID_HIGHEST + 1, // Debug > Run fake parallel job (6001).
        ID_CANCEL, // Optimize > Cancel: stops the running job, whatever started it (6002).

        ID_ANALYZE, // Drive > Analyze.
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
    };

    // Enables or disables a command everywhere it appears in frame: its menu item and any button with the same
    // id. Called explicitly on state changes: wxMSW refreshes wxEVT_UPDATE_UI for menus only when a menu opens,
    // and ignores accelerators of disabled items.
    void enableCommand(wxFrame& frame, int id, bool enabled);

} // namespace icd::ui
