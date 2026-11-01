#include "ui/panels/SummaryPage.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"

#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <format>
#include <initializer_list>
#include <string>

namespace icd::ui {

    namespace {

        // Appends a row of texts to a report list.
        void addRow(wxListView& list, std::initializer_list<std::string> cells) {
            const long row = list.GetItemCount();
            long column = 0;
            for (const std::string& cell : cells) {
                if (column == 0) {
                    list.InsertItem(row, wxString::FromUTF8(cell));
                }
                else {
                    list.SetItem(row, column, wxString::FromUTF8(cell));
                }
                ++column;
            }
        }

        // A one-decimal percentage.
        std::string percent(double value) { return std::format("{:.1f}%", value); }

    } // namespace

    SummaryPage::SummaryPage(wxWindow* parent) : wxPanel(parent) {
        hint_ = new wxStaticText(this, wxID_ANY, "");
        hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        metrics_ = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        metrics_->AppendColumn("Volume", wxLIST_FORMAT_LEFT, FromDIP(200));
        metrics_->AppendColumn("Value", wxLIST_FORMAT_RIGHT, FromDIP(110));
        classes_ = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        classes_->AppendColumn("Class", wxLIST_FORMAT_LEFT, FromDIP(170));
        classes_->AppendColumn("Files", wxLIST_FORMAT_RIGHT, FromDIP(90));
        classes_->AppendColumn("Size", wxLIST_FORMAT_RIGHT, FromDIP(90));
        classes_->AppendColumn("In target zone", wxLIST_FORMAT_RIGHT, FromDIP(110));

        auto* tables = new wxBoxSizer(wxHORIZONTAL);
        tables->Add(metrics_, wxSizerFlags(0).Expand().Border(wxRIGHT, FromDIP(8)));
        tables->Add(classes_, wxSizerFlags(1).Expand());
        metrics_->SetMinSize(FromDIP(wxSize(340, -1)));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(hint_, wxSizerFlags().Border(wxALL, FromDIP(8)));
        sizer->Add(tables, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
        SetSizer(sizer);
        clear("Analyze a drive to see fragmentation, free-space runs and per-class counts and bytes.");
    }

    void SummaryPage::show(const services::AnalysisSummary& summary) {
        metrics_->DeleteAllItems();
        classes_->DeleteAllItems();
        addRow(*metrics_, {"Fragmentation", percent(summary.fragmentationPercent)});
        addRow(*metrics_, {"Files", core::formatCount(summary.files)});
        addRow(*metrics_, {"Fragmented files", core::formatCount(summary.fragmentedFiles)});
        addRow(*metrics_, {"Extra fragments", core::formatCount(summary.fragments)});
        addRow(*metrics_, {"Free space", core::formatBytes(summary.freeBytes)});
        addRow(*metrics_, {"Free-space runs", core::formatCount(summary.freeRuns)});
        addRow(*metrics_, {"Largest free run", core::formatBytes(summary.largestFreeRun)});
        if (summary.scan) {
            addScanRows(*summary.scan);
        }
        for (const services::ClassSummary& row : summary.classes) {
            addRow(*classes_,
                   {row.name, core::formatCount(row.files), core::formatBytes(row.bytes), percent(row.inZonePercent)});
        }
        hint_->Hide();
        Layout();
    }

    void SummaryPage::addScanRows(const services::AnalysisSummary::ScanFigures& scan) {
        addRow(*metrics_, {"Directories", core::formatCount(scan.directories)});
        addRow(*metrics_, {"Data in fragmented files", percent(scan.fragmentedBytesPercent)});
        addRow(*metrics_, {"Free space in runs of 64 MB+", percent(scan.largeRunFreePercent)});
        addRow(*metrics_, {"Unmovable", core::formatBytes(scan.unmovableBytes)});
        if (scan.unreadableFiles != 0) {
            addRow(*metrics_, {"Unreadable files", core::formatCount(scan.unreadableFiles)});
        }
        if (scan.changedDuringScan != 0) {
            addRow(*metrics_, {"Changed during the scan", core::formatCount(scan.changedDuringScan)});
        }
    }

    void SummaryPage::clear(const wxString& hint) {
        metrics_->DeleteAllItems();
        classes_->DeleteAllItems();
        hint_->SetLabelText(hint);
        hint_->Show();
        Layout();
    }

} // namespace icd::ui
