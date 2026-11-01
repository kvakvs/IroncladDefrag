#include "platform/win/Compression.h"

#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <windows.h>

#include <compressapi.h>

#include <stdexcept>
#include <string>

namespace icd::platform {

    namespace {

        // Closes a compressor handle.
        struct CompressorCloser {
            void operator()(COMPRESSOR_HANDLE h) const { CloseCompressor(h); }
        };

        // Closes a decompressor handle.
        struct DecompressorCloser {
            void operator()(DECOMPRESSOR_HANDLE h) const { CloseDecompressor(h); }
        };

        // Throws with the Win32 error text of the last call.
        [[noreturn]] void fail(const char* what) {
            throw std::runtime_error(std::string(what) + " failed: " + toUtf8(lastWinError()));
        }

    } // namespace

    std::vector<std::byte> XpressCodec::compress(std::span<const std::byte> raw) const {
        COMPRESSOR_HANDLE handle = nullptr;
        if (CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &handle) == FALSE) {
            fail("CreateCompressor");
        }
        const std::unique_ptr<std::remove_pointer_t<COMPRESSOR_HANDLE>, CompressorCloser> owner(handle);
        SIZE_T needed = 0;
        if (Compress(handle, raw.data(), raw.size(), nullptr, 0, &needed) == FALSE &&
            GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            fail("Compress");
        }
        std::vector<std::byte> out(needed);
        SIZE_T written = 0;
        if (Compress(handle, raw.data(), raw.size(), out.data(), out.size(), &written) == FALSE) {
            fail("Compress");
        }
        out.resize(written);
        return out;
    }

    std::vector<std::byte> XpressCodec::decompress(std::span<const std::byte> stored, std::size_t rawSize) const {
        DECOMPRESSOR_HANDLE handle = nullptr;
        if (CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &handle) == FALSE) {
            fail("CreateDecompressor");
        }
        const std::unique_ptr<std::remove_pointer_t<DECOMPRESSOR_HANDLE>, DecompressorCloser> owner(handle);
        std::vector<std::byte> out(rawSize);
        SIZE_T written = 0;
        if (Decompress(handle, stored.data(), stored.size(), out.data(), out.size(), &written) == FALSE ||
            written != rawSize) {
            throw core::SnapshotFormatError("snapshot file: a block does not decompress");
        }
        return out;
    }

} // namespace icd::platform
