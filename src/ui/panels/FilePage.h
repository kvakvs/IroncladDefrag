#pragma once

#include "core/map/HitTest.h"

#include <wx/panel.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class wxButton;
class wxFlexGridSizer;
class wxSearchCtrl;
class wxStaticText;

namespace icd::ui {

    // What the File tab shows about the selected file.
    struct FileView {
        std::string path; // Full path ("D:\dir\name"), or the path on the volume when the root is unknown.
        std::vector<core::FileFact> facts; // Size, fragments, type, class, times (core::describeFile()).
        std::size_t fragment = 0; // The fragment the map shows, from 0.
        std::size_t fragments = 0; // Fragments of the file (0: data in the MFT record).
        bool openable = false; // Show in Explorer applies (not NTFS metadata, the volume root is known).
    };

    // What the File tab's controls ask the map panel to do.
    struct FileActions {
        std::function<void(const wxString& query)> search; // Find file: opens Go to file with the query.
        std::function<void()> showInExplorer; // Opens the selected file's folder with it selected.
        std::function<void()> copyPath; // Copies the selected file's full path.
    };

    // The "File" tab of the bottom panel (T127): the file last clicked on the map or found by search, with its facts,
    // the file search, Show in Explorer, Copy path and fragment stepping. It replaces the toolbar that sat above the
    // map, which gives the map that row. Fragment stepping sends the View commands, so F3 and the buttons share one
    // path; Defragment this file has the command id ID_DEFRAG_FILE and is enabled with the other workflow commands.
    class FilePage : public wxPanel {
    public:
        // Creates the page with nothing selected.
        explicit FilePage(wxWindow* parent);

        // Sets what the controls do.
        void setActions(FileActions actions) { actions_ = std::move(actions); }

        // Shows a file.
        void showFile(const FileView& view);

        // Shows that no file is selected.
        void clear();

        // Enables the search (there is a map to search).
        void setSearchEnabled(bool enabled);

        // Text typed into the search field (Ctrl+F opens Go to file with it).
        [[nodiscard]] wxString query() const;

    private:
        // Creates the search field and the buttons.
        wxSizer* createToolbar();

        // Replaces the facts grid's rows.
        void showFacts(const std::vector<core::FileFact>& facts);

        FileActions actions_; // What the controls do.
        wxSearchCtrl* search_ = nullptr; // "Find file".
        wxButton* explorer_ = nullptr; // Show in Explorer.
        wxButton* copy_ = nullptr; // Copy path.
        wxButton* previous_ = nullptr; // Previous fragment.
        wxButton* next_ = nullptr; // Next fragment.
        wxStaticText* path_ = nullptr; // The file's path, or the hint without one.
        wxFlexGridSizer* facts_ = nullptr; // Label / value pairs.
    };

} // namespace icd::ui
