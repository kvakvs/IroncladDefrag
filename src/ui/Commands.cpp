#include "ui/Commands.h"

#include <wx/frame.h>
#include <wx/menu.h>

namespace icd::ui {

    void enableCommand(wxFrame& frame, int id, bool enabled) {
        if (wxMenuBar* bar = frame.GetMenuBar(); bar != nullptr && bar->FindItem(id) != nullptr) {
            bar->Enable(id, enabled);
        }
        if (wxWindow* control = frame.FindWindow(id); control != nullptr && control != &frame) {
            control->Enable(enabled);
        }
    }

} // namespace icd::ui
