#pragma once

#include "core/snapshot/SnapshotIO.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::platform {

    // Snapshot block compression with the Windows Compression API (XPRESS with Huffman coding, cabinet.dll): fast
    // enough to keep up with saving a 20 M-file snapshot on a few threads, and part of every Windows, so no library
    // is added. Safe to use from several threads at once (each call has its own compressor).
    class XpressCodec : public core::BlockCodec {
    public:
        // Id 1 in snapshot block headers.
        [[nodiscard]] std::uint32_t id() const override { return 1; }

        // Compresses a block; throws std::runtime_error when the API fails.
        [[nodiscard]] std::vector<std::byte> compress(std::span<const std::byte> raw) const override;

        // Decompresses a block; throws core::SnapshotFormatError when the bytes do not decode to rawSize bytes.
        [[nodiscard]] std::vector<std::byte> decompress(std::span<const std::byte> stored,
                                                        std::size_t rawSize) const override;
    };

} // namespace icd::platform
