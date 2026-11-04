#pragma once

#include <wx/dialog.h>

#include <string>

namespace icd::ui {

    // What the Windows schedule dialog is about.
    struct ScheduleQuestion {
        std::string drive; // "D: Data".
        bool scheduled = false; // Windows' scheduled optimization covers the drive now.
        bool simulated = false; // Fake drive list: only the simulated schedule changes.
    };

    // Explains that Windows' scheduled optimization ("Optimize Drives") may undo zone layouts and asks to take the
    // drive out of it, or to put it back (plan.md 10.4, plan.md 14 decision 3). OK confirms the change; Cancel is the
    // default button, so Enter alone never changes the system's schedule.
    class ScheduleDialog : public wxDialog {
    public:
        // Builds the dialog for one drive.
        ScheduleDialog(wxWindow* parent, const ScheduleQuestion& what);
    };

} // namespace icd::ui
