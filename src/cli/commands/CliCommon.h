#pragma once

#include "core/exec/Executor.h"
#include "core/job/Progress.h"
#include "core/model/DriveInfo.h"
#include "services/AppServices.h"
#include "services/scan/ScanStats.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Pieces the icd-cli commands share: volume lookup, checks, console progress and executors.
namespace icd::cli {

    // Progress on stderr, overwriting one line, so stdout stays clean for JSON.
    class ConsoleProgress : public core::ProgressSink {
    public:
        // Rewrites the progress line.
        void report(const core::Progress& p) override;

        // Ends the progress line.
        static void finish();
    };

    // "t", "T:" and "T:\" all become "T:\"; other paths get a trailing backslash.
    [[nodiscard]] std::string normalizeRoot(const std::wstring& volume);

    // The drive mounted at the root, or nullopt.
    [[nodiscard]] std::optional<core::DriveInfo> findDrive(services::AppServices& app, const std::string& root);

    // Explains why a drive cannot be analysed (missing, not NTFS, not elevated), or returns nullptr.
    [[nodiscard]] const char* refusal(const std::optional<core::DriveInfo>& drive);

    // The executor --threads asks for: none for 0 (use the app's pool), inline for 1, else an own pool.
    [[nodiscard]] std::unique_ptr<core::Executor> ownExecutor(std::size_t threads);

    // The scanner named "api" or "mft", or nullopt.
    [[nodiscard]] std::optional<services::ScannerKind> scannerNamed(std::wstring_view name);

} // namespace icd::cli
