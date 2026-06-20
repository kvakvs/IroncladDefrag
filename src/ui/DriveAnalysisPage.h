#pragma once

#include "../model/DomainTypes.h"

#include <wx/wx.h>

namespace icd {

// Shows the current read-only analysis snapshot for one drive document tab.
class DriveAnalysisPage : public wxPanel {
public:
    DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result);

    void UpdateResult(const AnalysisResult& result);
    const std::wstring& GetDriveRoot() const { return driveRoot; }

private:
    wxStaticText* title = nullptr;
    wxStaticText* volume = nullptr;
    wxStaticText* files = nullptr;
    wxStaticText* fragmentation = nullptr;
    wxStaticText* freeSpace = nullptr;
    wxStaticText* classificationSizes = nullptr;
    wxStaticText* classificationTypes = nullptr;
    wxStaticText* classificationRecency = nullptr;
    wxStaticText* classificationPlacement = nullptr;
    wxStaticText* classificationSafety = nullptr;
    wxStaticText* warnings = nullptr;
    wxStaticText* todo = nullptr;
    std::wstring driveRoot;
};

} // namespace icd
