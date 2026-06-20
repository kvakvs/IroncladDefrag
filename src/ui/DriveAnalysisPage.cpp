#include "../precompiled.h"
#include "DriveAnalysisPage.h"

#include <sstream>

namespace icd {

namespace {
std::wstring FormatBytes(byte_count64_t bytes)
{
    double value = static_cast<double>(bytes.getValue());
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        ++unitIndex;
    }

    std::wstringstream stream;
    stream.precision(unitIndex == 0 ? 0 : 1);
    stream << std::fixed << value << L" " << units[unitIndex];
    return stream.str();
}

std::wstring KindToString(DriveKind kind)
{
    switch (kind) {
    case DriveKind::Mechanical:
        return L"Mechanical";
    case DriveKind::SolidState:
        return L"SSD";
    case DriveKind::Removable:
        return L"Removable";
    case DriveKind::Network:
        return L"Network";
    case DriveKind::Unknown:
    default:
        return L"Unknown";
    }
}
} // namespace

DriveAnalysisPage::DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result)
    : wxPanel(parent, wxID_ANY)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    title = new wxStaticText(this, wxID_ANY, "");
    volume = new wxStaticText(this, wxID_ANY, "");
    files = new wxStaticText(this, wxID_ANY, "");
    fragmentation = new wxStaticText(this, wxID_ANY, "");
    freeSpace = new wxStaticText(this, wxID_ANY, "");
    warnings = new wxStaticText(this, wxID_ANY, "");
    todo = new wxStaticText(this, wxID_ANY, "Drive map TODO: render cluster map, zones, and planned moves in later phases.");

    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);

    sizer->Add(title, 0, wxALL | wxEXPAND, 12);
    sizer->Add(volume, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    sizer->Add(files, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    sizer->Add(fragmentation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    sizer->Add(freeSpace, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    sizer->Add(warnings, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    sizer->Add(todo, 0, wxALL | wxEXPAND, 12);

    SetSizer(sizer);
    UpdateResult(result);
}

void DriveAnalysisPage::UpdateResult(const AnalysisResult& result)
{
    driveRoot = result.drive.rootPath;

    std::wstringstream volumeText;
    volumeText << L"Volume: " << result.volume.label << L" (" << result.volume.fileSystem << L"), "
               << FormatBytes(result.volume.freeBytes) << L" free of " << FormatBytes(result.volume.totalBytes)
               << L", cluster size " << FormatBytes(result.volume.bytesPerCluster);

    std::wstringstream filesText;
    filesText << L"Files: " << result.stats.scannedFiles.getValue() << L" scanned, "
              << result.stats.skippedFiles.getValue() << L" skipped, "
              << result.stats.inaccessibleFiles.getValue() << L" inaccessible, "
              << result.stats.filesWithExtents.getValue() << L" with extent data.";

    std::wstringstream fragmentationText;
    fragmentationText << L"Fragmentation: " << result.stats.fragmentedFiles.getValue() << L" fragmented files, "
                      << result.stats.totalFragments.getValue() << L" total fragments.";

    std::wstringstream freeSpaceText;
    freeSpaceText << L"Free space: ";
    if (result.stats.freeSpaceMapAvailable) {
        freeSpaceText << result.stats.freeSpaceBlocks.getValue() << L" blocks, largest block "
                      << result.stats.largestFreeBlockSectors.getValue() << L" clusters.";
    } else {
        freeSpaceText << L"bitmap unavailable.";
    }

    std::wstringstream warningsText;
    warningsText << L"Capabilities: " << KindToString(result.drive.kind);
    if (!result.drive.capabilities.disabledReason.empty()) {
        warningsText << L"; " << result.drive.capabilities.disabledReason;
    }
    if (result.stats.cancelled) {
        warningsText << L"; analysis was cancelled.";
    }

    title->SetLabel(wxString::Format("Analysis: %s", wxString(result.drive.displayName)));
    volume->SetLabel(volumeText.str());
    files->SetLabel(filesText.str());
    fragmentation->SetLabel(fragmentationText.str());
    freeSpace->SetLabel(freeSpaceText.str());
    warnings->SetLabel(warningsText.str());
    Layout();
}

} // namespace icd
