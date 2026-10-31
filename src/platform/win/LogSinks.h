#pragma once

#include "core/log/Log.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace icd::platform {

    // %LOCALAPPDATA%\IroncladDefrag\logs: per-user, writable without extra rights, survives reinstalls.
    std::filesystem::path defaultLogDirectory();

    // Appends lines to <directory>/<baseName>.log and keeps a few rotated files (<baseName>.1.log is the
    // newest old one), so logs survive crashes (each line is flushed) without growing without bound.
    class FileSink : public core::logging::Sink {
    public:
        // Where and how much to keep.
        struct Options {
            std::filesystem::path directory; // Created if missing.
            std::string baseName; // File name without ".log", e.g. "IroncladDefrag".
            std::uint64_t maxBytes = 8ull << 20; // Rotate when the current file would exceed this.
            int keepFiles = 5; // Number of rotated files kept besides the current one.
        };

        // Opens (and creates) the log file; throws std::filesystem::filesystem_error or std::runtime_error.
        explicit FileSink(Options options);

        // Appends the line and a newline, rotating first if the file is full. Failures are ignored.
        void write(const core::logging::Record& record, std::string_view line) override;

        // Flushes the file stream.
        void flush() override;

        // Path of the current log file.
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        // Opens path_ for appending and reads its current size.
        void open();

        // Shifts <base>.N.log up by one, moves the current file to <base>.1.log and reopens.
        void rotate();

        // Path of rotated file number index (1 = newest).
        [[nodiscard]] std::filesystem::path rotatedPath(int index) const;

        Options options_; // Settings given at construction.
        std::filesystem::path path_; // <directory>/<baseName>.log.
        std::ofstream stream_; // Open append stream on path_.
        std::uint64_t size_ = 0; // Bytes in the current file, to decide when to rotate.
    };

    // Sends lines to the debugger (OutputDebugStringW), visible in the IDE output window or DebugView.
    class DebugOutputSink : public core::logging::Sink {
    public:
        // Converts the UTF-8 line to UTF-16 and passes it on with a newline.
        void write(const core::logging::Record& record, std::string_view line) override;
    };

} // namespace icd::platform
