#include "ui/Commands.h"

#include <wx/frame.h>
#include <wx/menu.h>

#include <array>

namespace icd::ui {

    namespace {

        constexpr std::array workflowCommandList{
            WorkflowCommand{.id = ID_ANALYZE, .label = "&Analyze", .startsGroup = false},
            WorkflowCommand{.id = ID_QUICK, .label = "&Quick", .startsGroup = false},
            WorkflowCommand{.id = ID_BUILD_PLAN, .label = "&Build plan", .startsGroup = false},
            WorkflowCommand{.id = ID_EXECUTE, .label = "&Execute...", .startsGroup = false},
            WorkflowCommand{.id = ID_PAUSE, .label = "&Pause", .startsGroup = true},
            WorkflowCommand{.id = ID_CANCEL, .label = "&Cancel", .startsGroup = false},
        };

    } // namespace

    std::span<const WorkflowCommand> workflowCommands() { return workflowCommandList; }

    void enableCommand(wxFrame& frame, int id, bool enabled) {
        if (wxMenuBar* bar = frame.GetMenuBar(); bar != nullptr && bar->FindItem(id) != nullptr) {
            bar->Enable(id, enabled);
        }
        if (wxWindow* control = frame.FindWindow(id); control != nullptr && control != &frame) {
            control->Enable(enabled);
        }
    }

} // namespace icd::ui
