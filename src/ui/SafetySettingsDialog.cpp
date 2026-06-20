#include "../precompiled.h"
#include "SafetySettingsDialog.h"

#include <cwctype>
#include <sstream>
#include <wx/sizer.h>

namespace icd {

namespace {
constexpr std::uint64_t MiB = 1024ull * 1024ull;
constexpr std::uint64_t GiB = 1024ull * MiB;

std::vector<std::wstring> SplitLines(const wxString& text)
{
    std::vector<std::wstring> lines;
    std::wstringstream input(text.ToStdWstring());
    std::wstring line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        const auto first = std::find_if_not(line.begin(), line.end(), [](wchar_t ch) {
            return std::iswspace(ch) != 0;
        });
        const auto last = std::find_if_not(line.rbegin(), line.rend(), [](wchar_t ch) {
            return std::iswspace(ch) != 0;
        }).base();
        if (first < last) {
            lines.emplace_back(first, last);
        }
    }
    return lines;
}

std::wstring NormalizeExtension(std::wstring extension)
{
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    if (!extension.empty() && extension.front() != L'.') {
        extension.insert(extension.begin(), L'.');
    }
    return extension;
}

std::optional<SizeExclusionRange> ParseRange(const std::wstring& text)
{
    const std::size_t dash = text.find(L'-');
    if (dash == std::wstring::npos) {
        return std::nullopt;
    }

    SizeExclusionRange range;
    const std::wstring minimum = text.substr(0, dash);
    const std::wstring maximum = text.substr(dash + 1);
    try {
        if (!minimum.empty()) {
            range.hasMinimum = true;
            range.minimumBytes = byte_count64_t(std::stoull(minimum) * MiB);
        }
        if (!maximum.empty()) {
            range.hasMaximum = true;
            range.maximumBytes = byte_count64_t(std::stoull(maximum) * MiB);
        }
    } catch (...) {
        return std::nullopt;
    }
    return range.hasMinimum || range.hasMaximum ? std::optional<SizeExclusionRange>(range) : std::nullopt;
}

wxTextCtrl* AddMultiline(wxWindow* parent, wxBoxSizer* sizer, const wxString& label, const wxString& help)
{
    sizer->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, 8);
    auto* text = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 82), wxTE_MULTILINE);
    sizer->Add(text, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    sizer->Add(new wxStaticText(parent, wxID_ANY, help), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    return text;
}
} // namespace

SafetySettingsDialog::SafetySettingsDialog(wxWindow* parent, const SafetySettings& settings)
    : wxDialog(parent, wxID_ANY, "Safety Settings", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    defaultDryRunOnly = new wxCheckBox(this, wxID_ANY, "Keep dry-run only enabled by default");
    excludeProtectedSystemPaths = new wxCheckBox(this, wxID_ANY, "Exclude protected Windows system paths");
    root->Add(defaultDryRunOnly, 0, wxALL, 8);
    root->Add(excludeProtectedSystemPaths, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* capRow = new wxBoxSizer(wxHORIZONTAL);
    capRow->Add(new wxStaticText(this, wxID_ANY, "Global maximum bytes to move (GiB, 0 unlimited)"),
                0,
                wxALIGN_CENTER_VERTICAL | wxALL,
                8);
    maximumBytesGiB = new wxSpinCtrl(this, wxID_ANY);
    maximumBytesGiB->SetRange(0, 1024 * 1024);
    capRow->Add(maximumBytesGiB, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
    root->Add(capRow, 0, wxEXPAND);

    excludedDirectories =
        AddMultiline(this, root, "Excluded directories", "One path or path fragment per line. Matching files are excluded.");
    excludedExtensions = AddMultiline(this, root, "Excluded extensions", "One extension per line, such as .iso or tmp.");
    excludedSizeRanges = AddMultiline(this, root, "Excluded size ranges (MiB)", "One range per line: min-max, min-, or -max.");

    root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
    SetSizerAndFit(root);
    SetMinSize(wxSize(600, 560));

    Bind(wxEVT_BUTTON, &SafetySettingsDialog::OnOk, this, wxID_OK);
    LoadSettings(settings);
}

SafetySettings SafetySettingsDialog::GetSafetySettings() const
{
    return safetySettings;
}

void SafetySettingsDialog::LoadSettings(const SafetySettings& settings)
{
    safetySettings = settings;
    defaultDryRunOnly->SetValue(settings.defaultDryRunOnly);
    excludeProtectedSystemPaths->SetValue(settings.excludeProtectedSystemPaths);
    maximumBytesGiB->SetValue(static_cast<int>((settings.globalMaximumBytesToMove.getValue() + GiB - 1) / GiB));

    std::wstringstream directories;
    for (const std::filesystem::path& path : settings.excludedDirectories) {
        directories << path.wstring() << L"\n";
    }
    excludedDirectories->SetValue(directories.str());

    std::wstringstream extensions;
    for (const std::wstring& extension : settings.excludedExtensions) {
        extensions << extension << L"\n";
    }
    excludedExtensions->SetValue(extensions.str());

    std::wstringstream ranges;
    for (const SizeExclusionRange& range : settings.excludedSizeRanges) {
        if (range.hasMinimum) {
            ranges << (range.minimumBytes.getValue() / MiB);
        }
        ranges << L"-";
        if (range.hasMaximum) {
            ranges << (range.maximumBytes.getValue() / MiB);
        }
        ranges << L"\n";
    }
    excludedSizeRanges->SetValue(ranges.str());
}

bool SafetySettingsDialog::StoreSettings()
{
    SafetySettings next;
    next.defaultDryRunOnly = defaultDryRunOnly->GetValue();
    next.excludeProtectedSystemPaths = excludeProtectedSystemPaths->GetValue();
    next.globalMaximumBytesToMove = byte_count64_t(static_cast<std::uint64_t>(maximumBytesGiB->GetValue()) * GiB);

    for (const std::wstring& line : SplitLines(excludedDirectories->GetValue())) {
        next.excludedDirectories.emplace_back(line);
    }
    for (const std::wstring& line : SplitLines(excludedExtensions->GetValue())) {
        next.excludedExtensions.push_back(NormalizeExtension(line));
    }
    for (const std::wstring& line : SplitLines(excludedSizeRanges->GetValue())) {
        const std::optional<SizeExclusionRange> range = ParseRange(line);
        if (!range.has_value()) {
            wxMessageBox(wxString::Format("Invalid size range: %s", wxString(line)), "Safety Settings", wxOK | wxICON_WARNING, this);
            return false;
        }
        next.excludedSizeRanges.push_back(*range);
    }

    safetySettings = std::move(next);
    return true;
}

void SafetySettingsDialog::OnOk(wxCommandEvent& event)
{
    if (StoreSettings()) {
        event.Skip();
    }
}

} // namespace icd
