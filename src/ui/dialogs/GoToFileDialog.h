#pragma once

#include "core/job/CancelToken.h"
#include "core/snapshot/Types.h"

#include <wx/dialog.h>
#include <wx/timer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class wxKeyEvent;
class wxListEvent;
class wxListView;
class wxStaticText;
class wxTextCtrl;

namespace icd::core {
    class Executor;
    class MapData;
} // namespace icd::core

namespace icd::ui {

    struct SearchMailbox;

    // "Go to file" (plan.md 7.4): a search field over the analysed drive's names and paths with the matches in a
    // list; the chosen file is selected on the map and the view moves to its first fragment. Searches run on the
    // pool (a 20 M-file volume takes noticeable time), restart as the user types, and the results come back
    // through a mailbox the dialog detaches when it closes.
    class GoToFileDialog : public wxDialog {
    public:
        // One listed match, prepared on the worker so the UI thread only fills the list.
        struct Row {
            core::FileIndex file = core::noFile; // The file.
            std::string path; // Its path from the volume root.
            std::uint64_t sizeBytes = 0; // Logical size.
            std::size_t fragments = 0; // Fragments on disk (0: no clusters).
        };

        // Opens over map with query in the field and searches it at once; searches run on executor, which must
        // outlive the dialog.
        GoToFileDialog(wxWindow* parent, std::shared_ptr<const core::MapData> map, core::Executor& executor,
                       const wxString& query);

        // Cancels a running search and stops its delivery.
        ~GoToFileDialog() override;

        GoToFileDialog(const GoToFileDialog&) = delete;
        GoToFileDialog& operator=(const GoToFileDialog&) = delete;

        // The file picked, if the dialog was accepted with a match selected.
        [[nodiscard]] std::optional<core::FileIndex> chosen() const;

        // Takes the results of a search on the UI thread (called through the mailbox); results of an older search
        // are dropped.
        void showResults(std::uint64_t search, std::vector<Row> rows, std::size_t total);

    private:
        // Lays out the field, the list, the status line and the buttons.
        void createControls(const wxString& query);

        // Cancels the running search and starts one for the field's text.
        void startSearch();

        // Typing restarts a short timer, so a search starts once typing pauses.
        void onText(wxCommandEvent& event);

        // Enter in the field: accept the selected match (searching first if typing has not settled).
        void onTextEnter(wxCommandEvent& event);

        // Down arrow in the field moves into the list.
        void onTextKey(wxKeyEvent& event);

        // Double-click or Enter on a match accepts it.
        void onActivated(wxListEvent& event);

        // The Go to button is enabled while a match is selected.
        void updateButtons();

        std::shared_ptr<const core::MapData> map_; // Searched; kept alive for running searches by their copies.
        core::Executor& executor_; // Runs searches.
        std::shared_ptr<SearchMailbox> mailbox_; // Route from the worker back to this dialog.
        core::CancelSource cancel_; // Cancels the running search.
        std::uint64_t search_ = 0; // Number of the latest search; older results are dropped.
        wxTimer typingTimer_; // Starts a search once typing pauses.
        std::vector<Row> rows_; // Listed matches.
        wxTextCtrl* query_ = nullptr; // Search field; owned by wx.
        wxListView* list_ = nullptr; // Matches; owned by wx.
        wxStaticText* status_ = nullptr; // "12 matches"; owned by wx.
    };

} // namespace icd::ui
