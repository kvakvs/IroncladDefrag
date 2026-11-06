#pragma once

#include "core/map/Activity.h"
#include "core/map/ColorModes.h"
#include "core/map/Ruler.h"
#include "core/snapshot/Types.h"

#include <wx/panel.h>
#include <wx/weakref.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>


namespace icd::core {
    class Executor;
    class MapData;
    struct ClusterRun;
    struct MapOverlay;
} // namespace icd::core

namespace icd::ui {

    class FilePage;
    class Legend;
    class MapActivity;
    class MapCanvas;
    class MapColorsPage;
    struct FileView;
    class Ruler;

    // The drive map area (plan.md section 11): the map canvas with its rulers (plan.md 7.1) and nothing else, so the
    // map gets the height. The color mode switch and the legend (plan.md 7.4) live in the bottom panel's Map colors
    // tab, the selected file, its facts and the file search in the File tab; this panel keeps both in step. The drive
    // and its state are on the one status line.
    class MapPanel : public wxPanel {
    public:
        // Creates the toolbar and an empty map; tiles render on executor.
        MapPanel(wxWindow* parent, core::Executor& executor);

        // Disconnects the rulers from the canvas.
        ~MapPanel() override;

        MapPanel(const MapPanel&) = delete;
        MapPanel& operator=(const MapPanel&) = delete;

        // Feeds the Map colors tab from now on: its legend counts the view, its switch shows the mode. The page may be
        // destroyed first when the window closes; the panel then stops feeding it.
        void attachColors(MapColorsPage& page);

        // Feeds the File tab from now on (selected file, facts, search state) and takes its actions. Held like the
        // Map colors tab.
        void attachFile(FilePage& page);

        // Opens "Go to file" with query; the chosen file is selected and shown.
        void goToFile(const wxString& query);

        // Sets the receiver of selection changes (the workflow enables Defragment this file by it).
        void setSelectionHandler(std::function<void()> handler) { onSelection_ = std::move(handler); }

        // The selected file of the shown map, if any.
        [[nodiscard]] std::optional<core::FileIndex> selectedFile() const;

        // The selected file's full path (volume root and path on the volume), or empty without one.
        [[nodiscard]] std::string selectedPath() const;

        // Whether a View command can run: zone fit and plan preview need a plan's overlays.
        [[nodiscard]] bool commandAvailable(int id) const;

        // The overlays of the plan under review (null: none); in those color modes the map shows them. Clearing them
        // while one is shown goes back to the fragmentation mode.
        void setPlanOverlays(std::shared_ptr<const core::MapOverlay> zoneFit,
                             std::shared_ptr<const core::MapOverlay> preview);

        // Draws the map and the legend in a color scheme (preset and the user's colors).
        void setColorScheme(const core::ColorScheme& scheme);

        // Colors the map and the legend by a mode and shows it in the mode switch.
        void setColorMode(core::ColorMode mode);

        // The View > Color mode command of the current mode (for its check mark).
        [[nodiscard]] int colorModeCommand() const;

        // Shows a map, or clears it with null.
        void setMap(std::shared_ptr<const core::MapData> map);

        // Shows newer data of the same volume, keeping the view (MapCanvas::updateMap()).
        void updateMap(std::shared_ptr<const core::MapData> map);

        // Shows newer data where only the clusters in `dirty` changed (moves during execution): only the tiles over
        // them render again.
        void updateMap(std::shared_ptr<const core::MapData> map, std::span<const core::ClusterRun> dirty);

        // The root of the volume shown (a drive letter with its backslash), for full paths of files; empty for none.
        void setVolumeRoot(std::string root) { root_ = std::move(root); }

        // Where the selected volume sits on its disk (rulers' "% of disk"); nullopt when unknown.
        void setDisk(std::optional<core::DiskPlacement> disk);

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const;

        // The map shown, or null.
        [[nodiscard]] std::shared_ptr<const core::MapData> currentMap() const;

        // Carries out a View map command (color mode, minimap toggle, zoom in/out, fit, one cluster per pixel, go to
        // file, next/previous fragment); false for other ids.
        bool handleViewCommand(int id);

        // Whether the minimap is switched on (for the View > Minimap check mark).
        [[nodiscard]] bool minimapShown() const;

        // The map's read and write pulses (T114).
        [[nodiscard]] MapActivity& activity() const;

        // A read pulse over the part of the MFT an analysis read between two progress reports: along $MFT's extents
        // when the map shown already has them, else along `mft` (its first cluster and size from the volume data).
        void pulseMftRead(core::ClusterRun mft, core::WorkShare read);

        // A simulated move of the fake drive list's execution (item `index`): a read and then a write pulse over some
        // file of the map, so the pulses can be seen without a real volume.
        void pulseSimulatedMove(std::size_t index);

    private:
        // $MFT's data runs on the map shown (found by the extent at its first cluster), or `mft` when the map does
        // not have it yet (only the bitmap was read).
        [[nodiscard]] std::vector<core::ClusterRun> mftRuns(core::ClusterRun mft) const;

        // Shows the selection in the File tab and reports it.
        void showSelection();

        // What the File tab shows for the selection; nullopt without one.
        [[nodiscard]] std::optional<FileView> selectionView() const;

        // Copies the selected file's full path to the clipboard.
        void copySelectedPath();

        // Opens the selected file's folder in Explorer with the file selected; says so when it is gone.
        void showSelectedInExplorer();

        // The attached tab's legend, or null without one.
        [[nodiscard]] Legend* legend() const;

        // Shows the mode in the attached tab's switch and enables the modes that can be shown now.
        void syncColors();

        // Color mode and minimap commands; false for other ids.
        bool handleDisplayCommand(int id);

        // Zoom, go to file and fragment commands; false for other ids.
        bool handleNavigationCommand(int id);


        // Right click on the map: selects the file under the pointer, then the file menu.
        void onRightUp(wxMouseEvent& event);

        // The keyboard's menu key: the file menu for the selected file (mouse clicks come through onRightUp()).
        void onContextMenu(wxContextMenuEvent& event);

        // Shows Show in Explorer and Copy path for the selected file; nothing on free space.
        void showFileMenu();

        // Show in Explorer and Copy path for the selected file.
        void onFileCommand(wxCommandEvent& event);

        core::Executor& executor_; // Runs tiles and file searches.
        std::shared_ptr<const core::MapData> map_; // The map shown, for searches; null for none.
        std::string root_; // Root of the volume shown, for full paths.
        MapCanvas* canvas_ = nullptr; // The map; child window owned by wx.
        wxWeakRef<MapColorsPage> colors_; // The Map colors tab; becomes null when wx destroys it.
        wxWeakRef<FilePage> filePage_; // The File tab; becomes null when wx destroys it.
        std::function<void()> onSelection_; // Receiver of selection changes.
        const std::chrono::time_zone* zone_ = nullptr; // File times are shown in it (null: UTC).
        Ruler* rows_ = nullptr; // Row ruler left of the map; child window owned by wx.
        core::ColorMode mode_ = core::ColorMode::Fragmentation; // Current color mode.
        core::ColorScheme scheme_; // Colors of every mode.
        std::shared_ptr<const core::MapOverlay> zoneFit_; // Zone-fit overlay of the plan, or null.
        std::shared_ptr<const core::MapOverlay> preview_; // Plan-preview overlay of the plan, or null.
    };

} // namespace icd::ui
