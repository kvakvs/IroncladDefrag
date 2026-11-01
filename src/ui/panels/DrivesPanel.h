#pragma once

#include "core/model/DriveInfo.h"

#include <wx/panel.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class wxDataViewCtrl;
class wxDataViewEvent;

namespace icd::ui {

    class DriveListModel;

    // The all-disks panel (plan.md section 11): one row per volume with letter and label, a used/free bar,
    // file system, HDD/SSD, fragmentation, last analysis and badges. SSD rows carry only a TRIM button. The
    // panel owns the list it shows and reports the selected row; what a selection means is up to the frame.
    class DrivesPanel : public wxPanel {
    public:
        // Called with the selected row, or nullopt when nothing is selected.
        using SelectionHandler = std::function<void(std::optional<std::size_t>)>;

        // Creates an empty list.
        explicit DrivesPanel(wxWindow* parent);

        // Replaces the list, keeping the selection on the same volume root when it is still there.
        void setDrives(std::vector<core::DriveInfo> drives);

        // The drives shown, in row order.
        [[nodiscard]] const std::vector<core::DriveInfo>& drives() const { return drives_; }

        // Replaces the row with the same root (e.g. new analysis results); unknown roots are ignored.
        void updateDrive(const core::DriveInfo& drive);

        // The selected drive, or null.
        [[nodiscard]] const core::DriveInfo* selected() const;

        // Selects a row (nullopt clears) and reports it like a click would.
        void select(std::optional<std::size_t> row);

        // Sets the receiver of selection changes.
        void setSelectionHandler(SelectionHandler handler) { onSelection_ = std::move(handler); }

    private:
        // Adds the columns and their renderers.
        void createColumns();

        // Sizes the list to show every row without scrolling (up to a limit).
        void fitHeight();

        // Reports a selection change from the control.
        void onSelectionChanged(wxDataViewEvent& event);

        // Shows the TRIM button's tooltip (action and shortcut) while the pointer is over a button.
        void onListMotion(wxMouseEvent& event);

        // A TRIM button was clicked: selects its row and sends the Drive > TRIM command up to the frame.
        void requestTrim(std::size_t row);

        // The row currently selected in the control.
        [[nodiscard]] std::optional<std::size_t> selectedRow() const;

        std::vector<core::DriveInfo> drives_; // Rows, read by the model.
        wxDataViewCtrl* view_ = nullptr; // The list control; child owned by wx.
        DriveListModel* model_ = nullptr; // Owned by view_ (reference counted); valid while view_ lives.
        SelectionHandler onSelection_; // Receiver of selection changes.
    };

} // namespace icd::ui
