#pragma once

#include <wx/datetime.h>
#include <wx/panel.h>

#include <map>

class wxListView;
class wxStaticText;

namespace icd::ui {

    // The Log tab (plan.md 11): files that were skipped or did not move as planned, with the reason, and lines about
    // each run (totals, the snapshot, where the execution log is); TRIM output too. A line above the list counts the
    // rows per result, so thousands of rows stay readable. New rows scroll into view while the newest row is visible.
    class LogTab : public wxPanel {
    public:
        // Creates an empty list.
        explicit LogTab(wxWindow* parent);

        // Adds a row stamped with the current time.
        void add(const wxString& file, const wxString& result, const wxString& details);

        // Adds a row stamped with the time it happened (local time, to the second).
        void add(const wxString& file, const wxString& result, const wxString& details, const wxDateTime& when);

        // Empties the list (a new run starts).
        void clear();

    private:
        // Shows the counts per result.
        void updateCounts();

        wxListView* list_ = nullptr; // Time, file, result, details; child owned by wx.
        wxStaticText* counts_ = nullptr; // "3 skipped, 1 dropped"; child owned by wx.
        std::map<wxString, std::size_t> perResult_; // Rows per result, except lines about runs.
    };

} // namespace icd::ui
