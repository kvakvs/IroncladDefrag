#include "ui/dialogs/GoToFileDialog.h"

#include "core/exec/Executor.h"
#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/log/Log.h"
#include "core/map/MapData.h"
#include "core/snapshot/FileSearch.h"
#include "ui/AppIcon.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <exception>
#include <format>
#include <mutex>
#include <utility>

namespace icd::ui {

    // Where search workers send their results: the dialog while it is open, nobody after it closed. CallAfter only
    // queues an event, which a destroyed window drops (as for the map's tiles).
    struct SearchMailbox {
        std::mutex mutex; // Guards target.
        GoToFileDialog* target = nullptr; // Receiver; null after the dialog is gone.
    };

    namespace {

        // Matches listed; the status line tells how many more there are.
        constexpr std::size_t resultLimit = 500;

        // Pause in typing before a search starts.
        constexpr int typingMilliseconds = 200;

        // List columns.
        enum Column : std::uint8_t {
            PathColumn, // Path from the volume root.
            SizeColumn, // Logical size.
            FragmentsColumn, // Fragments on disk.
        };

        // The rows of a search result.
        std::vector<GoToFileDialog::Row> rowsFor(const core::Snapshot& snapshot, const core::SearchResult& result) {
            std::vector<GoToFileDialog::Row> rows;
            rows.reserve(result.hits.size());
            for (const core::SearchHit& hit : result.hits) {
                rows.push_back({.file = hit.file,
                                .path = snapshot.path(hit.file),
                                .sizeBytes = snapshot.files.sizeBytes(hit.file),
                                .fragments = snapshot.extents.fragmentCount(hit.file)});
            }
            return rows;
        }

        // Hands results to the dialog, if it is still open.
        void post(const std::shared_ptr<SearchMailbox>& mailbox, std::uint64_t search,
                  std::vector<GoToFileDialog::Row> rows, std::size_t total) {
            const std::scoped_lock lock(mailbox->mutex);
            GoToFileDialog* dialog = mailbox->target;
            if (dialog == nullptr) {
                return;
            }
            dialog->CallAfter([dialog, search, rows = std::move(rows), total]() mutable {
                dialog->showResults(search, std::move(rows), total);
            });
        }

        // The status line for a finished search.
        std::string statusFor(std::size_t shown, std::size_t total) {
            if (total == 0) {
                return "No matches";
            }
            if (shown < total) {
                return std::format("{} matches, the first {} are listed", core::formatCount(total),
                                   core::formatCount(shown));
            }
            return total == 1 ? std::string("1 match") : std::format("{} matches", core::formatCount(total));
        }

    } // namespace

    GoToFileDialog::GoToFileDialog(wxWindow* parent, std::shared_ptr<const core::MapData> map, core::Executor& executor,
                                   const wxString& query) :
        wxDialog(parent, wxID_ANY, "Go to file", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        map_(std::move(map)), executor_(executor), mailbox_(std::make_shared<SearchMailbox>()), typingTimer_(this) {
        SetIcons(appIcons());
        mailbox_->target = this;
        createControls(query);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { startSearch(); }, typingTimer_.GetId());
        startSearch();
    }

    GoToFileDialog::~GoToFileDialog() {
        cancel_.cancel();
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void GoToFileDialog::createControls(const wxString& query) {
        auto* hint = new wxStaticText(this, wxID_ANY,
                                      "Type a file name or part of a path, such as photos\\2024\\img. Letters match "
                                      "in any case.");
        query_ = new wxTextCtrl(this, wxID_ANY, query, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        list_ =
            new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(760, 360)), wxLC_REPORT | wxLC_SINGLE_SEL);
        list_->AppendColumn("Path", wxLIST_FORMAT_LEFT, FromDIP(540));
        list_->AppendColumn("Size", wxLIST_FORMAT_RIGHT, FromDIP(100));
        list_->AppendColumn("Fragments", wxLIST_FORMAT_RIGHT, FromDIP(90));
        status_ = new wxStaticText(this, wxID_ANY, "");

        auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
        if (auto* ok = dynamic_cast<wxButton*>(FindWindow(wxID_OK)); ok != nullptr) {
            ok->SetLabel("&Go to");
        }

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int gap = FromDIP(8);
        sizer->Add(hint, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(12)));
        sizer->Add(query_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(12)));
        sizer->Add(list_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(12)));
        sizer->Add(status_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap));
        sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();

        query_->Bind(wxEVT_TEXT, &GoToFileDialog::onText, this);
        query_->Bind(wxEVT_TEXT_ENTER, &GoToFileDialog::onTextEnter, this);
        query_->Bind(wxEVT_KEY_DOWN, &GoToFileDialog::onTextKey, this);
        list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &GoToFileDialog::onActivated, this);
        list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { updateButtons(); });
        list_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { updateButtons(); });
        query_->SetFocus();
        query_->SetInsertionPointEnd();
        updateButtons();
    }

    std::optional<core::FileIndex> GoToFileDialog::chosen() const {
        const long selected = list_->GetFirstSelected();
        if (selected < 0 || static_cast<std::size_t>(selected) >= rows_.size()) {
            return std::nullopt;
        }
        return rows_[static_cast<std::size_t>(selected)].file;
    }

    void GoToFileDialog::startSearch() {
        typingTimer_.Stop();
        cancel_.cancel();
        cancel_ = core::CancelSource();
        ++search_;
        const std::string query(query_->GetValue().utf8_str());
        if (query.find_first_not_of(" \t") == std::string::npos) {
            showResults(search_, {}, 0);
            status_->SetLabelText("");
            return;
        }
        status_->SetLabelText("Searching...");
        executor_.submit(
            [map = map_, query, mailbox = mailbox_, token = cancel_.token(), search = search_, &executor = executor_] {
                try {
                    const core::SearchResult result =
                        core::searchFiles(map->snapshot(), query, resultLimit, executor, token);
                    post(mailbox, search, rowsFor(map->snapshot(), result), result.total);
                }
                catch (const core::OperationCancelled&) {
                    core::logging::debug("file search cancelled"); // Replaced by a newer search, or the dialog closed.
                }
                catch (const std::exception& error) {
                    core::logging::error("file search failed", {{"what", error.what()}});
                }
            });
    }

    void GoToFileDialog::showResults(std::uint64_t search, std::vector<Row> rows, std::size_t total) {
        if (search != search_) {
            return;
        }
        rows_ = std::move(rows);
        list_->Freeze();
        list_->DeleteAllItems();
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            const Row& row = rows_[index];
            const auto item = static_cast<long>(index);
            list_->InsertItem(item, wxString::FromUTF8(row.path));
            list_->SetItem(item, SizeColumn, wxString::FromUTF8(core::formatBytes(row.sizeBytes)));
            list_->SetItem(item, FragmentsColumn,
                           row.fragments == 0 ? wxString("none")
                                              : wxString::FromUTF8(core::formatCount(row.fragments)));
        }
        if (!rows_.empty()) {
            list_->Select(0);
            list_->Focus(0);
        }
        list_->Thaw();
        status_->SetLabelText(wxString::FromUTF8(statusFor(rows_.size(), total)));
        updateButtons();
    }

    void GoToFileDialog::onText(wxCommandEvent& /*event*/) { typingTimer_.StartOnce(typingMilliseconds); }

    void GoToFileDialog::onTextEnter(wxCommandEvent& /*event*/) {
        if (typingTimer_.IsRunning()) {
            startSearch(); // Enter again once the matches are listed.
            return;
        }
        if (chosen()) {
            EndModal(wxID_OK);
        }
    }

    void GoToFileDialog::onTextKey(wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_DOWN && list_->GetItemCount() > 0) {
            list_->SetFocus();
            return;
        }
        event.Skip();
    }

    void GoToFileDialog::onActivated(wxListEvent& /*event*/) {
        if (chosen()) {
            EndModal(wxID_OK);
        }
    }

    void GoToFileDialog::updateButtons() {
        if (wxWindow* ok = FindWindow(wxID_OK); ok != nullptr) {
            ok->Enable(chosen().has_value());
        }
    }

} // namespace icd::ui
