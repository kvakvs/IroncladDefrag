#pragma once

#include "../model/DomainTypes.h"

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>

namespace icd {

// Shows an inspectable dry-run move plan without offering execution controls.
class MovePlanDialog : public wxDialog {
public:
    MovePlanDialog(wxWindow* parent, const MovePlan& plan);

private:
    void PopulateOperations(const MovePlan& plan);
    void PopulateNotes(const MovePlan& plan);

    wxListCtrl* operations = nullptr;
    wxTextCtrl* notes = nullptr;
};

} // namespace icd
