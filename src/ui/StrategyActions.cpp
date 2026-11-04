#include "ui/StrategyActions.h"

#include "services/settings/StrategyLibrary.h"

#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace icd::ui {

    namespace {

        // File types of the import and export dialogs.
        constexpr const char* scriptFiles = "Zone scripts (*.json)|*.json|All files (*.*)|*.*";

    } // namespace

    StrategyActions::StrategyActions(wxWindow& parent, StrategyPage& page, services::StrategyLibrary& library) :
        parent_(parent), page_(page), library_(library) {}

    void StrategyActions::showLibrary(std::string_view id) {
        const services::EffortLevel effort = page_.effort();
        const std::string keep(id); // id may point into the page's list, which is replaced.
        std::vector<core::strategy::ZoneScript> list;
        for (const core::strategy::ZoneScript* script : library_.all()) {
            list.push_back(*script);
        }
        page_.setStrategies(std::move(list));
        page_.select(keep, effort);
    }

    bool StrategyActions::run(StrategyCommand command) {
        switch (command) {
        case StrategyCommand::SaveAs:
            return saveAs();
        case StrategyCommand::Delete:
            return remove();
        case StrategyCommand::Import:
            return importFile();
        case StrategyCommand::Export:
            exportFile();
            return false;
        }
        return false;
    }

    bool StrategyActions::saveAs() {
        const core::strategy::ZoneScript shown = page_.script();
        wxString name = wxGetTextFromUser("Name of the custom strategy:", "Save strategy",
                                          wxString::FromUTF8(shown.name), &parent_);
        name.Trim().Trim(false);
        if (name.empty()) {
            return false; // Cancelled.
        }
        const std::string utf8 = name.utf8_string();
        const auto& custom = library_.custom();
        const auto same = std::ranges::find(custom, utf8, &core::strategy::ZoneScript::name);
        const bool replacing = same != custom.end() && same->id != page_.presetId();
        if (replacing &&
            wxMessageBox("A custom strategy named \"" + name + "\" exists. Replace it?", "Save strategy",
                         wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, &parent_) != wxYES) {
            return false;
        }
        try {
            const std::string id = library_.save(shown, utf8).id;
            showLibrary(id);
            return true;
        }
        catch (const services::StrategyLibraryError& error) {
            showError("Save strategy", error.what());
            return false;
        }
    }

    bool StrategyActions::remove() {
        if (!page_.customSelected()) {
            return false;
        }
        const std::string id = page_.presetId();
        const wxString question =
            "Delete the custom strategy \"" + wxString::FromUTF8(page_.script().name) + "\"? This cannot be undone.";
        if (wxMessageBox(question, "Delete strategy", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, &parent_) != wxYES) {
            return false;
        }
        try {
            (void)library_.remove(id);
            showLibrary({}); // The default preset; drives that remembered it fall back to it too.
            return true;
        }
        catch (const services::StrategyLibraryError& error) {
            showError("Delete strategy", error.what());
            return false;
        }
    }

    bool StrategyActions::importFile() {
        wxFileDialog dialog(&parent_, "Import strategy", wxEmptyString, wxEmptyString, scriptFiles,
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return false;
        }
        try {
            const std::string id = library_.importFile(std::filesystem::path(dialog.GetPath().ToStdWstring())).id;
            showLibrary(id);
            return true;
        }
        catch (const services::StrategyLibraryError& error) {
            showError("Import strategy", error.what());
            return false;
        }
    }

    void StrategyActions::exportFile() {
        const core::strategy::ZoneScript& shown = page_.script();
        wxFileDialog dialog(&parent_, "Export strategy", wxEmptyString, wxString::FromUTF8(shown.id + ".json"),
                            scriptFiles, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        try {
            services::StrategyLibrary::exportFile(shown, std::filesystem::path(dialog.GetPath().ToStdWstring()));
        }
        catch (const services::StrategyLibraryError& error) {
            showError("Export strategy", error.what());
        }
    }

    void StrategyActions::showError(const wxString& title, const char* message) {
        wxMessageBox(wxString::FromUTF8(message), title, wxOK | wxICON_ERROR, &parent_);
    }

} // namespace icd::ui
