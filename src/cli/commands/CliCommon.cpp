#include "cli/commands/CliCommon.h"

#include "core/strategy/Presets.h"
#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>

namespace icd::cli {

    void ConsoleProgress::report(const core::Progress& p) {
        if (p.total != 0) {
            std::fprintf(stderr, "\r%-24s %5.1f%%   ", p.stage.c_str(), 100.0 * p.fraction());
        }
        else {
            std::fprintf(stderr, "\r%-24s          ", p.stage.c_str());
        }
    }

    void ConsoleProgress::finish() { std::fprintf(stderr, "\r%60s\r", ""); }

    std::string normalizeRoot(const std::wstring& volume) {
        std::wstring root = volume;
        if (root.size() <= 2 && std::iswalpha(root[0]) != 0) {
            root = std::wstring(1, static_cast<wchar_t>(std::towupper(root[0]))) + L":";
        }
        if (root.back() != L'\\') {
            root += L'\\';
        }
        return platform::toUtf8(root);
    }

    std::optional<core::DriveInfo> findDrive(services::AppServices& app, const std::string& root) {
        for (core::DriveInfo& drive : app.drives().listDrives()) {
            const bool same = std::ranges::equal(drive.root, root, [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
            if (same) {
                return std::move(drive);
            }
        }
        return std::nullopt;
    }

    const char* refusal(const std::optional<core::DriveInfo>& drive) {
        if (!drive) {
            return "no mounted volume at that path";
        }
        if (drive->fileSystem != "NTFS") {
            return "only NTFS volumes can be analysed";
        }
        if (!platform::isElevated()) {
            return "analysis needs administrator rights; run it from an elevated prompt";
        }
        return nullptr;
    }

    std::unique_ptr<core::Executor> ownExecutor(std::size_t threads) {
        if (threads == 0) {
            return nullptr;
        }
        if (threads == 1) {
            return std::make_unique<core::InlineExecutor>();
        }
        return std::make_unique<services::ThreadPool>(threads);
    }

    std::optional<core::strategy::Effort> effortNamed(std::wstring_view name) {
        if (name == L"fast") {
            return core::strategy::Effort::Fast;
        }
        if (name == L"moderate") {
            return core::strategy::Effort::Moderate;
        }
        if (name == L"max" || name == L"maximum") {
            return core::strategy::Effort::Maximum;
        }
        return std::nullopt;
    }

    const core::strategy::ZoneScript* presetNamed(const std::string& id) {
        const auto index = core::strategy::builtinPresetIndex(id);
        return index ? &core::strategy::builtinPresets()[*index] : nullptr;
    }

    std::string presetIds() {
        std::string ids;
        for (const auto& p : core::strategy::builtinPresets()) {
            ids += (ids.empty() ? "" : ", ") + p.id;
        }
        return ids;
    }

    std::optional<services::ScannerKind> scannerNamed(std::wstring_view name) {
        if (name == L"api") {
            return services::ScannerKind::Api;
        }
        if (name == L"mft") {
            return services::ScannerKind::Mft;
        }
        return std::nullopt;
    }

} // namespace icd::cli
