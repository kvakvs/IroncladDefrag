#include "ui/panels/MapPanel.h"

#include "core/map/MapData.h"
#include "services/ShellActions.h"
#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/dialogs/GoToFileDialog.h"
#include "ui/map/Legend.h"
#include "ui/map/MapCanvas.h"
#include "ui/map/MapNavigator.h"
#include "ui/map/MapSelection.h"
#include "ui/map/Ruler.h"
#include "ui/panels/FilePage.h"
#include "ui/panels/MapColorsPage.h"

#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace icd::ui {

    namespace {

        // The View > Color mode command of a mode.
        int commandOf(core::ColorMode mode) {
            const auto items = colorModeItems();
            const auto found = std::ranges::find(items, mode, &ColorModeItem::mode);
            return found == items.end() ? ID_COLOR_FRAGMENTATION : found->command;
        }

        // The time zone file times are shown in; null (UTC) when the system's cannot be found.
        const std::chrono::time_zone* localZone() {
            try {
                return std::chrono::current_zone();
            }
            catch (const std::exception&) {
                return nullptr;
            }
        }

    } // namespace

    MapPanel::MapPanel(wxWindow* parent, core::Executor& executor) :
        wxPanel(parent), executor_(executor), zone_(localZone()) {
        canvas_ = new MapCanvas(this, executor);
        auto* rows = new Ruler(this, *canvas_, Ruler::Orientation::Vertical);
        rows_ = rows;
        auto* columns = new Ruler(this, *canvas_, Ruler::Orientation::Horizontal);
        canvas_->setViewObserver([this, rows, columns] {
            rows->Refresh(false);
            columns->Refresh(false);
            if (Legend* counts = legend(); counts != nullptr) {
                counts->setView(canvas_->viewport());
            }
        });

        // Rulers above and left of the map; the corner stays empty.
        auto* map = new wxFlexGridSizer(2, 2, 0, 0);
        map->AddGrowableCol(1);
        map->AddGrowableRow(1);
        map->AddSpacer(0);
        map->Add(columns, wxSizerFlags().Expand());
        map->Add(rows, wxSizerFlags().Expand());
        map->Add(canvas_, wxSizerFlags().Expand());

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(map, wxSizerFlags(1).Expand().Border(wxTOP, FromDIP(4)));
        SetSizer(sizer);
        canvas_->selection().setObserver([this] { showSelection(); });

        // The release itself, not wxEVT_CONTEXT_MENU: the canvas does not get that from a mouse click.
        canvas_->Bind(wxEVT_RIGHT_UP, &MapPanel::onRightUp, this);
        canvas_->Bind(wxEVT_CONTEXT_MENU, &MapPanel::onContextMenu, this);
        // The popup's commands start at the canvas and travel up to this panel.
        Bind(wxEVT_MENU, &MapPanel::onFileCommand, this, ID_SHOW_IN_EXPLORER, ID_COPY_PATH);
    }

    MapPanel::~MapPanel() {
        canvas_->setViewObserver({}); // The rulers may be destroyed before the canvas.
        canvas_->selection().setObserver({}); // So may the File tab.
    }

    void MapPanel::attachFile(FilePage& page) {
        filePage_ = &page;
        page.setActions({.search = [this](const wxString& query) { goToFile(query); },
                         .showInExplorer = [this] { showSelectedInExplorer(); },
                         .copyPath = [this] { copySelectedPath(); }});
        page.setSearchEnabled(map_ != nullptr);
        showSelection();
    }

    void MapPanel::showSelection() {
        if (FilePage* page = filePage_.get(); page != nullptr) {
            const std::optional<FileView> view = selectionView();
            if (view) {
                page->showFile(*view);
            }
            else {
                page->clear();
            }
        }
        if (onSelection_) {
            onSelection_();
        }
    }

    std::optional<FileView> MapPanel::selectionView() const {
        const MapSelection& selection = canvas_->selection();
        const auto file = selection.file();
        if (!file || map_ == nullptr) {
            return std::nullopt;
        }
        const core::Snapshot& snapshot = map_->snapshot();
        const std::string full = selectedPath();
        return FileView{.path = full.empty() ? snapshot.path(*file) : full,
                        .facts = core::describeFile(snapshot, *file, zone_, map_->fileClasses()),
                        .fragment = selection.fragment(),
                        .fragments = selection.fragmentCount(),
                        .openable = !full.empty() && !snapshot.files.has(*file, core::FileMetadata)};
    }

    std::optional<core::FileIndex> MapPanel::selectedFile() const {
        return map_ != nullptr ? canvas_->selection().file() : std::nullopt;
    }

    void MapPanel::attachColors(MapColorsPage& page) {
        colors_ = &page;
        page.legend().setScheme(scheme_);
        page.legend().setMode(mode_);
        page.legend().setMap(map_);
        if (canvas_->hasMap()) {
            page.legend().setView(canvas_->viewport());
        }
        syncColors();
    }

    Legend* MapPanel::legend() const {
        MapColorsPage* page = colors_.get();
        return page != nullptr ? &page->legend() : nullptr;
    }

    void MapPanel::syncColors() {
        MapColorsPage* page = colors_.get();
        if (page == nullptr) {
            return;
        }
        for (const ColorModeItem& item : colorModeItems()) {
            page->enableMode(item.mode, hasMap() && commandAvailable(item.command));
        }
        page->showMode(mode_);
    }

    bool MapPanel::commandAvailable(int id) const {
        if (id == ID_COLOR_ZONE_FIT) {
            return zoneFit_ != nullptr;
        }
        return id != ID_COLOR_PLAN || preview_ != nullptr;
    }

    void MapPanel::setColorMode(core::ColorMode mode) {
        if (!commandAvailable(commandOf(mode))) {
            mode = core::ColorMode::Fragmentation; // No plan yet: the overlay modes have nothing to show.
        }
        mode_ = mode;
        canvas_->setPalette(core::paletteFor(mode, scheme_));
        canvas_->setOverlay(mode == core::ColorMode::ZoneFit           ? zoneFit_
                                : mode == core::ColorMode::PlanPreview ? preview_
                                                                       : nullptr);
        if (Legend* counts = legend(); counts != nullptr) {
            counts->setMode(mode);
        }
        syncColors();
    }

    void MapPanel::setColorScheme(const core::ColorScheme& scheme) {
        scheme_ = scheme;
        if (Legend* counts = legend(); counts != nullptr) {
            counts->setScheme(scheme_);
        }
        setColorMode(mode_); // Repaints with the new palette.
    }

    void MapPanel::setPlanOverlays(std::shared_ptr<const core::MapOverlay> zoneFit,
                                   std::shared_ptr<const core::MapOverlay> preview) {
        zoneFit_ = std::move(zoneFit);
        preview_ = std::move(preview);
        setColorMode(mode_); // Shows the new overlays, or leaves an overlay mode that lost its plan.
    }

    int MapPanel::colorModeCommand() const { return commandOf(mode_); }

    void MapPanel::setMap(std::shared_ptr<const core::MapData> map) {
        map_ = map;
        if (FilePage* page = filePage_.get(); page != nullptr) {
            page->setSearchEnabled(map_ != nullptr);
        }
        if (Legend* counts = legend(); counts != nullptr) {
            counts->setMap(map); // Before the canvas, which reports its first view.
        }
        canvas_->setMap(std::move(map));
        syncColors();
        showSelection(); // A new map starts without a selection.
    }

    void MapPanel::updateMap(std::shared_ptr<const core::MapData> map) {
        map_ = map;
        if (FilePage* page = filePage_.get(); page != nullptr) {
            page->setSearchEnabled(map_ != nullptr);
        }
        if (Legend* counts = legend(); counts != nullptr) {
            counts->updateMap(map);
        }
        canvas_->updateMap(std::move(map));
        syncColors();
    }

    void MapPanel::updateMap(std::shared_ptr<const core::MapData> map, std::span<const core::ClusterRun> dirty) {
        map_ = map;
        if (Legend* counts = legend(); counts != nullptr) {
            counts->updateMap(map);
        }
        canvas_->updateMap(std::move(map), dirty);
    }

    void MapPanel::setDisk(std::optional<core::DiskPlacement> disk) { rows_->setDisk(disk); }

    void MapPanel::goToFile(const wxString& query) {
        if (map_ == nullptr) {
            return;
        }
        GoToFileDialog dialog(this, map_, executor_, query);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        if (const auto file = dialog.chosen(); file) {
            canvas_->selection().goTo(*file);
            canvas_->SetFocus(); // Map keys and F3 work on from here.
        }
    }

    void MapPanel::onRightUp(wxMouseEvent& event) {
        event.Skip();
        if (map_ == nullptr) {
            return;
        }
        canvas_->selection().selectAt({static_cast<double>(event.GetX()), static_cast<double>(event.GetY())});
        showFileMenu();
    }

    void MapPanel::onContextMenu(wxContextMenuEvent& event) {
        if (event.GetPosition() != wxDefaultPosition) {
            return; // From the mouse: onRightUp() has it.
        }
        showFileMenu();
    }

    void MapPanel::showFileMenu() {
        const std::optional<core::FileIndex> file = canvas_->selection().file();
        if (!file || map_ == nullptr) {
            return; // Free space: nothing to offer.
        }
        const bool metadata = map_->snapshot().files.has(*file, core::FileMetadata); // $MFT and friends are hidden.
        wxMenu menu;
        menu.Append(ID_SHOW_IN_EXPLORER, "Show in &Explorer");
        menu.Append(ID_COPY_PATH, "&Copy path");
        menu.Enable(ID_SHOW_IN_EXPLORER, !metadata && !root_.empty());
        menu.Enable(ID_COPY_PATH, !root_.empty());
        canvas_->PopupMenu(&menu);
    }

    std::string MapPanel::selectedPath() const {
        const auto file = canvas_->selection().file();
        if (!file || map_ == nullptr || root_.empty()) {
            return {};
        }
        return services::volumePath(root_, map_->snapshot().path(*file));
    }

    void MapPanel::onFileCommand(wxCommandEvent& event) {
        if (event.GetId() == ID_COPY_PATH) {
            copySelectedPath();
        }
        else {
            showSelectedInExplorer();
        }
    }

    void MapPanel::copySelectedPath() {
        const std::string path = selectedPath();
        if (!path.empty() && wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(path))); // The clipboard owns it.
            wxTheClipboard->Close();
        }
    }

    void MapPanel::showSelectedInExplorer() {
        const std::string path = selectedPath();
        if (path.empty()) {
            return;
        }
        if (!services::showInExplorer(path)) {
            wxMessageBox(wxString::FromUTF8(path +
                                            "\n\nwas not found. It may have been moved, renamed or deleted "
                                            "since the analysis; analyze again to refresh the map."),
                         "Show in Explorer", wxOK | wxICON_INFORMATION, this);
        }
    }

    bool MapPanel::hasMap() const { return canvas_->hasMap(); }

    std::shared_ptr<const core::MapData> MapPanel::currentMap() const { return canvas_->sharedMap(); }

    bool MapPanel::minimapShown() const { return canvas_->minimap().shown(); }

    bool MapPanel::handleViewCommand(int id) { return handleDisplayCommand(id) || handleNavigationCommand(id); }

    bool MapPanel::handleDisplayCommand(int id) {
        if (id == ID_SHOW_MINIMAP) {
            canvas_->showMinimap(!canvas_->minimap().shown());
            return true;
        }
        const auto items = colorModeItems();
        const auto item = std::ranges::find(items, id, &ColorModeItem::command);
        if (item == items.end()) {
            return false;
        }
        setColorMode(item->mode);
        return true;
    }

    bool MapPanel::handleNavigationCommand(int id) {
        MapNavigator& navigator = canvas_->navigator();
        const core::Viewport& view = canvas_->viewport();
        const core::PixelPoint center{view.size.x / 2.0, view.size.y / 2.0};
        switch (id) {
        case ID_ZOOM_IN:
            navigator.zoomBy(2.0, center);
            return true;
        case ID_ZOOM_OUT:
            navigator.zoomBy(0.5, center);
            return true;
        case ID_ZOOM_FIT:
            navigator.fit();
            return true;
        case ID_ZOOM_CLUSTER:
            navigator.zoomToScale(1.0);
            return true;
        case ID_GOTO_FILE:
            goToFile(filePage_.get() != nullptr ? filePage_->query() : wxString());
            return true;
        case ID_NEXT_FRAGMENT:
            canvas_->selection().nextFragment();
            return true;
        case ID_PREVIOUS_FRAGMENT:
            canvas_->selection().previousFragment();
            return true;
        default:
            return false;
        }
    }

} // namespace icd::ui
