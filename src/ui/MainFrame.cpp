#include "ui/MainFrame.h"

#include "services/AppInfo.h"

namespace icd {

    MainFrame::MainFrame() :
        wxFrame(nullptr, wxID_ANY,
                wxString::FromUTF8(std::string(services::appName()) + " " + std::string(services::appVersion())),
                wxDefaultPosition, wxDefaultSize) {
        SetMinSize(FromDIP(wxSize(640, 480)));
        SetSize(FromDIP(wxSize(1280, 800)));
        Centre();
    }

} // namespace icd
