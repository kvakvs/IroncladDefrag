#pragma once

#include "core/resources/ResourceCaps.h"
#include "services/settings/Settings.h"

#include <wx/dialog.h>

#include <cstdint>

class wxCheckBox;
class wxChoice;
class wxRadioBox;
class wxSpinCtrl;
class wxStaticText;

namespace icd::services {
    class AppServices;
}

namespace icd::ui {

    // The page the Settings dialog opens on, so Settings > Resources lands on the caps directly.
    enum class SettingsPage : std::uint8_t {
        Planning, // Fast budget multiplier.
        Resources, // Thread and memory caps.
        Appearance, // Light or dark windows.
    };

    // What the Resources page needs to know about the machine to offer and preview caps.
    struct ResourceFacts {
        core::DetectedResources detected; // Processors and memory right now.
        std::uint32_t maxThreads = 1; // Most worker threads a manual cap may ask for (the pool's capacity).
    };

    // Settings > Preferences and Settings > Resources (plan.md 13 Phase 10): the Fast budget multiplier (9.2.1),
    // manual thread and memory caps (6.6, Auto by default) with a preview of the caps they give, and light or dark
    // windows (applied at the next start). Edits a copy of the settings; the caller saves and applies settings() after
    // OK.
    class SettingsDialog : public wxDialog {
    public:
        // Builds the dialog on a copy of settings, showing page first.
        SettingsDialog(wxWindow* parent, const services::Settings& settings, const ResourceFacts& facts,
                       SettingsPage page);

        // The settings with the dialog's choices.
        [[nodiscard]] services::Settings settings() const;

    private:
        // The Planning page: the Fast budget multiplier.
        wxWindow* createPlanningPage(wxWindow* parent);

        // The Resources page: Auto or manual threads and memory, and the caps they give.
        wxWindow* createResourcesPage(wxWindow* parent);

        // The Appearance page: System, Light or Dark windows.
        wxWindow* createAppearancePage(wxWindow* parent);

        // The manual caps as chosen (0 for Auto).
        [[nodiscard]] core::ResourceSettings resourceChoice() const;

        // Enables the manual fields that are not Auto and shows the caps the choice gives.
        void updateResources();

        services::Settings settings_; // The settings the dialog started from; fields it does not show are kept.
        ResourceFacts facts_; // The machine, for limits and the preview.
        wxChoice* multiplier_ = nullptr; // x1 / x2 / x4 / x10.
        wxCheckBox* autoThreads_ = nullptr; // Auto worker count.
        wxSpinCtrl* threads_ = nullptr; // Manual worker count.
        wxCheckBox* autoMemory_ = nullptr; // Auto memory budget.
        wxSpinCtrl* memoryGb_ = nullptr; // Manual memory budget in GB.
        wxStaticText* preview_ = nullptr; // The caps the choice gives, as describeCaps() words them.
        wxRadioBox* appearance_ = nullptr; // System / Light / Dark.
    };

    // Shows the Settings dialog for services' settings and, after OK, saves and applies the changes (caps take
    // effect for the next job, the appearance at the next start). Returns false when nothing changed; status receives
    // what the status line says.
    bool editSettings(wxWindow* parent, services::AppServices& services, SettingsPage page, wxString& status);

} // namespace icd::ui
