
#pragma once

#include <wx/wx.h>

namespace icd {

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title);
    virtual ~MainFrame();

private:
    void CreateMenuBar();
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

} // namespace icd
