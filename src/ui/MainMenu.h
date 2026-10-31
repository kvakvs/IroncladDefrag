#pragma once

#include <span>

class wxMenuBar;

namespace icd::ui {

    // Builds the top menu from plan.md section 11 (File, Drive, Optimize, View, Settings, Help). Kept
    // apart from MainFrame so the menu layout is one table, not scattered Append calls. The caller owns the
    // result until it hands it to a frame.
    wxMenuBar* createMainMenuBar();

    // Every command in the menu whose enabled state follows the app state (all but Exit and About), so the frame can
    // refresh them in one loop.
    std::span<const int> stateDependentCommands();

} // namespace icd::ui
