#pragma once

#include <wx/dialog.h>

#include <cstdint>
#include <string>

namespace icd::ui {

    // What the VSS warning reports about the chosen plan level.
    struct VssWarningFacts {
        std::string drive; // "D: Data".
        std::uint64_t unalignedMoves = 0; // Moves not aligned to shadow-copy blocks.
        std::uint64_t moves = 0; // All moves of the level.
        std::uint64_t growthBytes = 0; // Estimated shadow storage growth.
    };

    // Shown before Execute when the drive has shadow copies and the plan has unaligned moves (plan.md 9.4): they make
    // the shadow copies grow, and Windows deletes the oldest copies when their storage is full. Offers to block them
    // and plan again (blockAndReplan), to execute anyway (wxID_OK) or to cancel (the default).
    class VssWarningDialog : public wxDialog {
    public:
        // ShowModal() result of "Block unaligned moves and plan again".
        static constexpr int blockAndReplan = wxID_YES;

        // Builds the dialog for one plan level.
        VssWarningDialog(wxWindow* parent, const VssWarningFacts& facts);
    };

} // namespace icd::ui
