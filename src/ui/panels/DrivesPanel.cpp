#include "ui/panels/DrivesPanel.h"

#include "core/format/DriveText.h"
#include "ui/Accelerators.h"
#include "ui/Commands.h"

#include <wx/dataview.h>
#include <wx/dc.h>
#include <wx/renderer.h>
#include <wx/settings.h>
#include <wx/sizer.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace icd::ui {

    namespace {

        // Columns in display order.
        enum Column : std::uint8_t {
            ColumnName,
            ColumnSpace, // Used/free bar with text; custom renderer.
            ColumnFileSystem,
            ColumnMedia,
            ColumnFragmentation,
            ColumnLastAnalysed,
            ColumnBadges,
            ColumnAction, // TRIM button on SSD rows; custom renderer.
        };

        // Rows shown before the list starts to scroll.
        constexpr std::size_t maxVisibleRows = 8;

        // Used share above which the space bar turns red, like Explorer.
        constexpr double nearlyFull = 0.9;

    } // namespace

    // Serves the panel's drive vector to the control. Text columns are formatted on demand; the two custom
    // columns get the row index or a flag, and their renderers read what they need.
    class DriveListModel : public wxDataViewVirtualListModel {
    public:
        // Reads rows from drives, which the panel keeps alive.
        explicit DriveListModel(const std::vector<core::DriveInfo>& drives) : drives_(drives) {}

        // The drive in a row, or null for a stale row.
        [[nodiscard]] const core::DriveInfo* drive(std::size_t row) const {
            return row < drives_.size() ? &drives_[row] : nullptr;
        }

        // Fills one cell (signature fixed by wx; row and column travel on together as a Cell).
        void GetValueByRow(wxVariant& value, unsigned row, unsigned col) const override {
            value = cellValue(Cell{.row = row, .column = col});
        }

        // The list is read-only.
        bool SetValueByRow(const wxVariant& /*value*/, unsigned /*row*/, unsigned /*col*/) override { return false; }

    private:
        // A cell address, so row and column cannot be swapped on the way to the formatting code.
        struct Cell {
            unsigned row = 0; // Index into drives_.
            unsigned column = 0; // A Column value.
        };

        // The variant shown in a cell: text, the row index for the space bar, or the TRIM flag.
        [[nodiscard]] wxVariant cellValue(Cell cell) const {
            const core::DriveInfo* info = drive(cell.row);
            if (info == nullptr) {
                return wxString();
            }
            if (cell.column == ColumnSpace) {
                return static_cast<long>(cell.row);
            }
            if (cell.column == ColumnAction) {
                return core::isSsd(*info);
            }
            return wxString::FromUTF8(cellText(*info, cell.column));
        }

        // Text of a text column.
        static std::string cellText(const core::DriveInfo& info, unsigned col) {
            const auto now = std::chrono::system_clock::now();
            switch (col) {
            case ColumnName:
                return core::driveName(info);
            case ColumnFileSystem:
                return info.fileSystem;
            case ColumnMedia:
                return core::describeStorage(info);
            case ColumnFragmentation:
                return core::describeFragmentation(info);
            case ColumnLastAnalysed:
                return core::describeLastAnalysed(info, now);
            case ColumnBadges:
                return joinBadges(core::badgeLabels(info, now));
            default:
                return {};
            }
        }

        // Badges as one cell: "VSS · Last access on".
        static std::string joinBadges(const std::vector<std::string>& labels) {
            std::string text;
            for (const std::string& label : labels) {
                text += text.empty() ? label : " \xC2\xB7 " + label;
            }
            return text;
        }

        const std::vector<core::DriveInfo>& drives_; // Owned by the panel.
    };

    namespace {

        // Draws the used/free bar and the "free of total" text. The cell value is the row index, so the
        // renderer can read the drive without packing it into a wxVariant.
        class SpaceRenderer : public wxDataViewCustomRenderer {
        public:
            // Reads drives through model, which outlives the renderer (both belong to the control).
            explicit SpaceRenderer(const DriveListModel& model) : wxDataViewCustomRenderer("long"), model_(model) {}

            // Takes the row index.
            bool SetValue(const wxVariant& value) override {
                row_ = value.GetLong();
                return true;
            }

            // Returns the row index.
            bool GetValue(wxVariant& value) const override {
                value = row_;
                return true;
            }

            // Wide enough for the bar and "1.10 TB free of 4.00 TB".
            wxSize GetSize() const override { return GetView()->FromDIP(wxSize(260, 18)); }

            // Bar on the left, text on the right.
            bool Render(wxRect cell, wxDC* dc, int state) override {
                const core::DriveInfo* drive = model_.drive(static_cast<std::size_t>(row_));
                if (drive == nullptr) {
                    return false;
                }
                const wxWindow* view = GetView();
                const int barWidth = std::min(view->FromDIP(100), cell.width / 2);
                wxRect bar(cell.x, cell.y + view->FromDIP(5), barWidth, cell.height - view->FromDIP(10));
                drawBar(*dc, bar, drive->usedFraction());
                RenderText(wxString::FromUTF8(core::describeSpace(*drive)), barWidth + view->FromDIP(8), cell, dc,
                           state);
                return true;
            }

        private:
            // Frame and used part of the bar.
            static void drawBar(wxDC& dc, const wxRect& bar, double used) {
                const bool dark = wxSystemSettings::GetAppearance().IsUsingDarkBackground();
                dc.SetPen(wxPen(dark ? wxColour(110, 110, 110) : wxColour(160, 160, 160)));
                dc.SetBrush(wxBrush(dark ? wxColour(60, 60, 60) : wxColour(230, 230, 230))); // The free part.
                dc.DrawRectangle(bar);
                const int fill = static_cast<int>(used * (bar.width - 2));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(used > nearlyFull ? wxColour(218, 38, 38) : wxColour(38, 160, 218)));
                dc.DrawRectangle(bar.x + 1, bar.y + 1, fill, bar.height - 2);
            }

            const DriveListModel& model_; // Source of the drives.
            long row_ = 0; // Row of the cell being drawn.
        };

        // Draws a TRIM push button on SSD rows and reports clicks on it.
        class TrimRenderer : public wxDataViewCustomRenderer {
        public:
            // Calls onTrim with the row of a clicked button.
            explicit TrimRenderer(std::function<void(std::size_t)> onTrim) :
                wxDataViewCustomRenderer("bool", wxDATAVIEW_CELL_ACTIVATABLE, wxALIGN_CENTER),
                onTrim_(std::move(onTrim)) {}

            // Takes whether the row has a button.
            bool SetValue(const wxVariant& value) override {
                visible_ = value.GetBool();
                return true;
            }

            // Returns whether the row has a button.
            bool GetValue(wxVariant& value) const override {
                value = visible_;
                return true;
            }

            // Room for the button.
            wxSize GetSize() const override { return GetView()->FromDIP(wxSize(80, 22)); }

            // The button, centred in the cell.
            bool Render(wxRect cell, wxDC* dc, int /*state*/) override {
                if (!visible_) {
                    return true;
                }
                const wxRect button = buttonRect(cell);
                wxRendererNative::Get().DrawPushButton(GetView(), *dc, button, 0);
                dc->SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
                dc->DrawLabel("TRIM", button, wxALIGN_CENTER);
                return true;
            }

            // A click on the button, or Space/Enter on the row, runs TRIM for that row.
            bool ActivateCell(const wxRect& cell, wxDataViewModel* model, const wxDataViewItem& item, unsigned /*col*/,
                              const wxMouseEvent* mouse) override {
                const auto* list = dynamic_cast<wxDataViewVirtualListModel*>(model);
                if (list == nullptr) {
                    return false;
                }
                if (mouse != nullptr && !buttonRect(wxRect(cell.GetSize())).Contains(mouse->GetPosition())) {
                    return false; // Clicks beside the button only select the row.
                }
                onTrim_(list->GetRow(item));
                return true;
            }

        private:
            // Button area inside a cell.
            [[nodiscard]] wxRect buttonRect(const wxRect& cell) const {
                const wxSize size = GetView()->FromDIP(wxSize(64, 22));
                const wxSize used(std::min(size.x, cell.width), std::min(size.y, cell.height));
                return {cell.x + (cell.width - used.x) / 2, cell.y + (cell.height - used.y) / 2, used.x, used.y};
            }

            std::function<void(std::size_t)> onTrim_; // Receiver of clicks.
            bool visible_ = false; // Whether the current row has a button.
        };

    } // namespace

    DrivesPanel::DrivesPanel(wxWindow* parent) : wxPanel(parent) {
        view_ = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxDV_SINGLE | wxDV_ROW_LINES | wxDV_HORIZ_RULES);
        model_ = new DriveListModel(drives_);
        view_->AssociateModel(model_);
        model_->DecRef(); // The control keeps the only reference.
        view_->SetRowHeight(FromDIP(26));
        createColumns();
        view_->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &DrivesPanel::onSelectionChanged, this);
        view_->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &DrivesPanel::onContextMenu, this);
        view_->GetMainWindow()->Bind(wxEVT_MOTION, &DrivesPanel::onListMotion, this);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(view_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
        fitHeight();
    }

    void DrivesPanel::createColumns() {
        const auto text = [this](const char* title, unsigned col, int width) {
            view_->AppendTextColumn(title, col, wxDATAVIEW_CELL_INERT, FromDIP(width));
        };
        text("Drive", ColumnName, 120);
        view_->AppendColumn(new wxDataViewColumn("Space", new SpaceRenderer(*model_), ColumnSpace, FromDIP(260)));
        text("File system", ColumnFileSystem, 80);
        text("Media", ColumnMedia, 110);
        text("Fragmentation", ColumnFragmentation, 100);
        text("Last analysed", ColumnLastAnalysed, 140); // SSD rows show "trimmed 12 days ago" here.
        text("Badges", ColumnBadges, 320);
        auto* trim = new TrimRenderer([this](std::size_t row) { requestTrim(row); });
        view_->AppendColumn(new wxDataViewColumn("Action", trim, ColumnAction, FromDIP(90), wxALIGN_CENTER));
    }

    void DrivesPanel::fitHeight() {
        const std::size_t rows = std::clamp<std::size_t>(drives_.size(), 1, maxVisibleRows);
        const int height = static_cast<int>(rows + 1) * FromDIP(26) + FromDIP(6); // Rows plus the header.
        view_->SetMinSize(wxSize(-1, height));
        if (GetParent() != nullptr) {
            GetParent()->Layout();
        }
    }

    void DrivesPanel::setDrives(std::vector<core::DriveInfo> drives) {
        const core::DriveInfo* previous = selected();
        const std::string keep = previous != nullptr ? previous->root : std::string();
        drives_ = std::move(drives);
        model_->Reset(static_cast<unsigned>(drives_.size()));
        fitHeight();
        const auto match = std::ranges::find(drives_, keep, &core::DriveInfo::root);
        select(keep.empty() || match == drives_.end()
                   ? std::nullopt
                   : std::optional<std::size_t>(static_cast<std::size_t>(match - drives_.begin())));
    }

    void DrivesPanel::updateDrive(const core::DriveInfo& drive) {
        const auto match = std::ranges::find(drives_, drive.root, &core::DriveInfo::root);
        if (match == drives_.end()) {
            return;
        }
        *match = drive;
        model_->RowChanged(static_cast<unsigned>(match - drives_.begin()));
    }

    const core::DriveInfo* DrivesPanel::selected() const {
        const auto row = selectedRow();
        return row ? &drives_[*row] : nullptr;
    }

    void DrivesPanel::select(std::optional<std::size_t> row) {
        if (row && *row < drives_.size()) {
            view_->Select(model_->GetItem(static_cast<unsigned>(*row)));
        }
        else {
            view_->UnselectAll();
        }
        if (onSelection_) {
            onSelection_(selectedRow());
        }
    }

    std::optional<std::size_t> DrivesPanel::selectedRow() const {
        const wxDataViewItem item = view_->GetSelection();
        if (!item.IsOk()) {
            return std::nullopt;
        }
        const std::size_t row = model_->GetRow(item);
        return row < drives_.size() ? std::optional(row) : std::nullopt;
    }

    void DrivesPanel::onSelectionChanged(wxDataViewEvent& /*event*/) {
        if (onSelection_) {
            onSelection_(selectedRow());
        }
    }

    void DrivesPanel::onListMotion(wxMouseEvent& event) {
        event.Skip();
        wxWindow* rows = view_->GetMainWindow();
        wxDataViewItem item;
        wxDataViewColumn* column = nullptr;
        view_->HitTest(view_->ScreenToClient(rows->ClientToScreen(event.GetPosition())), item, column);
        const bool onTrim = item.IsOk() && column != nullptr && column->GetModelColumn() == ColumnAction &&
            core::isSsd(drives_.at(model_->GetRow(item)));
        // The TRIM button is drawn in a cell, so the rows window shows its tooltip while the pointer is over it.
        if (onTrim && rows->GetToolTipText() != commandTip(ID_TRIM)) {
            rows->SetToolTip(commandTip(ID_TRIM));
        }
        else if (!onTrim && rows->GetToolTip() != nullptr) {
            rows->UnsetToolTip();
        }
    }

    void DrivesPanel::onContextMenu(wxDataViewEvent& event) {
        const wxDataViewItem item = event.GetItem();
        if (!item.IsOk()) {
            return; // Below the last row.
        }
        const std::size_t row = model_->GetRow(item);
        if (selectedRow() != row) {
            select(row); // The menu acts on the row under the pointer, like a left click would pick it.
        }
        if (onContextMenu_ && selectedRow() == row) {
            onContextMenu_();
        }
    }

    void DrivesPanel::requestTrim(std::size_t row) {
        if (selectedRow() != row) {
            select(row);
        }
        // Queued, not processed here: the click is still inside the control's mouse handling. Command events
        // travel up to the frame.
        auto* command = new wxCommandEvent(wxEVT_MENU, ID_TRIM);
        command->SetEventObject(this);
        wxQueueEvent(this, command);
    }

} // namespace icd::ui
