#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace icd::core {

    // CRC-32 (IEEE 802.3, the zip/PNG polynomial) for the blocks of saved snapshots: catches torn writes and disk
    // damage before a corrupt snapshot is trusted for planning.
    class Crc32 {
    public:
        // Continues the checksum over more bytes.
        void update(std::span<const std::byte> bytes) {
            for (const std::byte b : bytes) {
                value_ = table()[(value_ ^ static_cast<std::uint8_t>(b)) & 0xFFU] ^ (value_ >> 8U);
            }
        }

        // The checksum of everything so far.
        [[nodiscard]] std::uint32_t value() const { return ~value_; }

        // Checksum of one buffer.
        [[nodiscard]] static std::uint32_t of(std::span<const std::byte> bytes) {
            Crc32 crc;
            crc.update(bytes);
            return crc.value();
        }

    private:
        // The byte-wise lookup table, built once at compile time.
        static const std::array<std::uint32_t, 256>& table() {
            static constexpr std::array<std::uint32_t, 256> values = [] {
                std::array<std::uint32_t, 256> t{};
                for (std::uint32_t i = 0; i < 256; ++i) {
                    std::uint32_t c = i;
                    for (int k = 0; k < 8; ++k) {
                        c = (c & 1U) != 0 ? 0xEDB8'8320U ^ (c >> 1U) : c >> 1U;
                    }
                    t[i] = c;
                }
                return t;
            }();
            return values;
        }

        std::uint32_t value_ = 0xFFFF'FFFFU; // Running register (inverted).
    };

} // namespace icd::core
