#include "../precompiled.h"
#include "MovePlanDialog.h"

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

std::wstring ZoneName(ExpectedPlacementZone zone)
{
    switch (zone) {
    case ExpectedPlacementZone::Fast:
        return L"Fast";
    case ExpectedPlacementZone::Balanced:
        return L"Balanced";
    case ExpectedPlacementZone::Slow:
        return L"Slow";
    case ExpectedPlacementZone::LargeFile:
        return L"Large-file";
    case ExpectedPlacementZone::None:
    default:
        return L"None";
    }
}

std::wstring RiskName(MoveRisk risk)
{
    switch (risk) {
    case MoveRisk::Low:
        return L"Low";
    case MoveRisk::Medium:
        return L"Medium";
    case MoveRisk::High:
        return L"High";
    default:
        return L"Unknown";
    }
}

std::wstring SkipReasonName(MoveSkipReason reason)
{
    switch (reason) {
    case MoveSkipReason::Excluded:
        return L"excluded";
    case MoveSkipReason::ExplicitOnly:
        return L"explicit-only";
    case MoveSkipReason::MissingExtents:
        return L"missing extents";
    case MoveSkipReason::AlreadyGoodEnough:
        return L"already good enough";
    case MoveSkipReason::OverBudget:
        return L"over budget";
    case MoveSkipReason::TooRisky:
        return L"too risky";
    case MoveSkipReason::NoDestinationRange:
        return L"no destination range";
    case MoveSkipReason::DisabledTargetZone:
        return L"disabled or missing target zone";
    case MoveSkipReason::None:
    default:
        return L"none";
    }
}
} // namespace

MovePlanDialog::MovePlanDialog(wxWindow* parent, const MovePlan& plan)
    : wxDialog(parent, wxID_ANY, "Dry-Run Move Plan", wxDefaultPosition, wxSize(900, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* summary = new wxStaticText(this, wxID_ANY, wxString(plan.summary));
    root->Add(summary, 0, wxEXPAND | wxALL, 8);

    operations = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    operations->AppendColumn("File", wxLIST_FORMAT_LEFT, 320);
    operations->AppendColumn("From", wxLIST_FORMAT_LEFT, 90);
    operations->AppendColumn("To", wxLIST_FORMAT_LEFT, 90);
    operations->AppendColumn("Bytes", wxLIST_FORMAT_RIGHT, 100);
    operations->AppendColumn("Fragments", wxLIST_FORMAT_LEFT, 90);
    operations->AppendColumn("Risk", wxLIST_FORMAT_LEFT, 80);
    operations->AppendColumn("Reason", wxLIST_FORMAT_LEFT, 260);
    root->Add(operations, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    notes = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 150), wxTE_MULTILINE | wxTE_READONLY);
    root->Add(notes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(CreateSeparatedButtonSizer(wxOK), 0, wxEXPAND | wxALL, 8);

    SetSizer(root);
    PopulateOperations(plan);
    PopulateNotes(plan);
}

void MovePlanDialog::PopulateOperations(const MovePlan& plan)
{
    long row = 0;
    for (const MoveOperation& operation : plan.operations) {
        operations->InsertItem(row, wxString(operation.filePath.wstring()));
        operations->SetItem(row, 1, wxString(ZoneName(operation.currentZone)));
        operations->SetItem(row, 2, wxString(ZoneName(operation.targetZone)));
        operations->SetItem(row, 3, wxString(FormatBytes(operation.estimatedBytes)));
        operations->SetItem(row,
                            4,
                            wxString::Format("%llu -> %llu",
                                             operation.fragmentCountBefore.getValue(),
                                             operation.fragmentCountAfterEstimate.getValue()));
        operations->SetItem(row, 5, wxString(RiskName(operation.risk)));
        operations->SetItem(row, 6, wxString(operation.reason));
        ++row;
    }
}

void MovePlanDialog::PopulateNotes(const MovePlan& plan)
{
    std::wstringstream text;
    text << L"Affected files: " << plan.metrics.affectedFiles.getValue() << L"\n";
    text << L"Skipped files: " << plan.metrics.skippedFiles.getValue() << L"\n";
    text << L"Expected zone changes: " << plan.metrics.expectedZoneChanges.getValue() << L"\n";
    text << L"Fragmentation improvement candidates: " << plan.metrics.fragmentationImprovementFiles.getValue() << L"\n";
    text << L"Free-space reservations: " << plan.metrics.freeSpaceReservations.getValue() << L"\n";
    text << L"Estimated bytes moved: " << FormatBytes(plan.estimatedBytesToMove) << L"\n";
    text << L"Status: " << (plan.impossible ? L"impossible" : (plan.partial ? L"partial" : L"complete dry-run")) << L"\n\n";

    if (!plan.issues.empty()) {
        text << L"Issues:\n";
        for (const MovePlanIssue& issue : plan.issues) {
            text << L"- " << (issue.blocking ? L"blocking: " : L"note: ") << issue.message << L"\n";
        }
        text << L"\n";
    }

    text << L"Skipped candidates:\n";
    const std::size_t skippedToShow = (std::min)(std::size_t(20), plan.skippedCandidates.size());
    for (std::size_t index = 0; index < skippedToShow; ++index) {
        const SkippedMoveCandidate& skipped = plan.skippedCandidates[index];
        text << L"- file #" << skipped.fileIndex << L": " << SkipReasonName(skipped.reason) << L" (" << skipped.detail
             << L")\n";
    }
    if (plan.skippedCandidates.size() > skippedToShow) {
        text << L"- ... " << (plan.skippedCandidates.size() - skippedToShow) << L" more skipped candidates\n";
    }

    text << L"\nCancellation and rollback notes:\n";
    const std::size_t operationsToShow = (std::min)(std::size_t(10), plan.operations.size());
    for (std::size_t index = 0; index < operationsToShow; ++index) {
        const MoveOperation& operation = plan.operations[index];
        text << L"- " << operation.filePath.wstring() << L": " << operation.cancellationBoundary << L"; "
             << operation.rollbackNote << L"\n";
    }

    notes->SetValue(text.str());
}

} // namespace icd
