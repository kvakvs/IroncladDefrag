#pragma once

#include <wx/wx.h>

namespace icd {

    class App final : public wxApp {
    public:
        App();
        virtual ~App();

        virtual bool OnInit() override;
        virtual int OnExit() override;

    private:
        wxDECLARE_EVENT_TABLE();
    };

} // namespace icd
