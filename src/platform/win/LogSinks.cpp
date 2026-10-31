#include "platform/win/LogSinks.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <stdexcept>
#include <system_error>
#include <utility>

namespace icd::platform {

    namespace {

        // UTF-8 to UTF-16 for Win32 calls; invalid bytes become U+FFFD instead of failing.
        std::wstring widen(std::string_view utf8) {
            if (utf8.empty()) {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
            return wide;
        }

    } // namespace

    std::filesystem::path defaultLogDirectory() {
        PWSTR folder = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
        std::filesystem::path base;
        if (SUCCEEDED(result) && folder != nullptr) {
            base = folder;
        }
        CoTaskMemFree(folder);
        if (base.empty()) {
            base = std::filesystem::temp_directory_path();
        }
        return base / L"IroncladDefrag" / L"logs";
    }

    FileSink::FileSink(Options options) : options_(std::move(options)) {
        std::filesystem::create_directories(options_.directory);
        path_ = options_.directory / std::filesystem::path(widen(options_.baseName + ".log"));
        open();
        if (size_ >= options_.maxBytes) {
            rotate();
        }
    }

    void FileSink::open() {
        stream_.open(path_, std::ios::binary | std::ios::app);
        if (!stream_) {
            const std::u8string name = path_.u8string();
            throw std::runtime_error("cannot open log file " + std::string(name.begin(), name.end()));
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(path_, ec);
        size_ = ec ? 0 : size;
    }

    std::filesystem::path FileSink::rotatedPath(int index) const {
        return options_.directory / std::filesystem::path(widen(std::format("{}.{}.log", options_.baseName, index)));
    }

    void FileSink::rotate() {
        stream_.close();
        std::error_code ec; // Rotation is best effort: a locked old file must not stop logging.
        std::filesystem::remove(rotatedPath(options_.keepFiles), ec);
        for (int index = options_.keepFiles - 1; index >= 1; --index) {
            std::filesystem::rename(rotatedPath(index), rotatedPath(index + 1), ec);
        }
        if (options_.keepFiles > 0) {
            std::filesystem::rename(path_, rotatedPath(1), ec);
        }
        else {
            std::filesystem::remove(path_, ec);
        }
        stream_.clear();
        stream_.open(path_, std::ios::binary | std::ios::trunc);
        size_ = 0;
    }

    void FileSink::write(const core::logging::Record& /*record*/, std::string_view line) {
        const std::uint64_t bytes = line.size() + 2;
        if (size_ > 0 && size_ + bytes > options_.maxBytes) {
            rotate();
        }
        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        stream_.write("\r\n", 2);
        stream_.flush();
        size_ += bytes;
    }

    void FileSink::flush() { stream_.flush(); }

    void DebugOutputSink::write(const core::logging::Record& /*record*/, std::string_view line) {
        std::wstring wide = widen(line);
        wide += L"\n";
        OutputDebugStringW(wide.c_str());
    }

} // namespace icd::platform
