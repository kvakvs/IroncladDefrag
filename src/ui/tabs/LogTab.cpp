#include "ui/tabs/LogTab.h"

#include <wx/datetime.h>
#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace icd::ui {

    namespace {

        // Results that describe a run or a tool's output rather than a file, left out of the counts.
        bool countable(const wxString& result) { return result != "run" && result != "TRIM"; }

    } // namespace

    LogTab::LogTab(wxWindow* parent) : wxPanel(parent) {
        counts_ = new wxStaticText(this, wxID_ANY, "");
        counts_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        list_ = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->AppendColumn("Time", wxLIST_FORMAT_LEFT, FromDIP(140));
        list_->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(420));
        list_->AppendColumn("Result", wxLIST_FORMAT_LEFT, FromDIP(110));
        list_->AppendColumn("Details", wxLIST_FORMAT_LEFT, FromDIP(420));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(counts_, wxSizerFlags().Border(wxALL, FromDIP(4)));
        sizer->Add(list_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
        updateCounts();
    }

    void LogTab::add(const wxString& file, const wxString& result, const wxString& details) {
        const long count = list_->GetItemCount();
        // Follow new rows only while the newest is in view, so reading older rows is not disturbed.
        const bool following = count == 0 || list_->GetTopItem() + list_->GetCountPerPage() >= count;
        const long row = list_->InsertItem(count, wxDateTime::Now().FormatISOCombined(' '));
        list_->SetItem(row, 1, file);
        list_->SetItem(row, 2, result);
        list_->SetItem(row, 3, details);
        if (following) {
            list_->EnsureVisible(row);
        }
        if (countable(result)) {
            ++perResult_[result];
        }
        updateCounts(); // Also the first run line: "nothing was skipped" needs a row to be true of.
    }

    void LogTab::clear() {
        list_->DeleteAllItems();
        perResult_.clear();
        updateCounts();
    }

    void LogTab::updateCounts() {
        wxString text;
        for (const auto& [result, rows] : perResult_) {
            text += wxString::Format("%s%zu %s", text.empty() ? "" : ", ", rows, result);
        }
        if (text.empty()) {
            text = list_->GetItemCount() == 0 ? wxString() : wxString("No file was skipped or moved otherwise.");
        }
        counts_->SetLabelText(text);
    }

} // namespace icd::ui
