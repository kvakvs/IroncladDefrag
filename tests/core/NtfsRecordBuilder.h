#pragma once

// Builds MFT records byte by byte for parser and scanner tests, the way NTFS lays them out on disk: header, attributes,
// end marker, and update sequence protection over every 512-byte block.

#include "core/ntfs/MftRecord.h"
#include "core/ntfs/NtfsFormat.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace icd::test {

    // A run for encodeRuns(): lcn -1 is a hole.
    struct RunSpec {
        std::int64_t lcn = -1; // First cluster, or -1 for a hole.
        std::uint64_t length = 0; // Clusters.
    };

    // Little-endian bytes of the smallest size that holds value as a signed (or unsigned) integer.
    inline std::vector<std::byte> minimalBytes(std::int64_t value, bool isSigned) {
        std::vector<std::byte> out;
        for (unsigned size = 1; size <= 8; ++size) {
            const std::int64_t shift = size == 8 ? 0 : 8 * size;
            const bool fitsSigned =
                size == 8 || (value >= -(std::int64_t{1} << (shift - 1)) && value < (std::int64_t{1} << (shift - 1)));
            const bool fitsUnsigned = size == 8 || static_cast<std::uint64_t>(value) < (std::uint64_t{1} << shift);
            if (isSigned ? fitsSigned : fitsUnsigned) {
                for (unsigned i = 0; i < size; ++i) {
                    out.push_back(static_cast<std::byte>(static_cast<std::uint64_t>(value) >> (8 * i)));
                }
                return out;
            }
        }
        return out;
    }

    // A run list (mapping pairs) for the runs, with the terminator.
    inline std::vector<std::byte> encodeRuns(const std::vector<RunSpec>& runs) {
        std::vector<std::byte> out;
        std::int64_t previous = 0;
        for (const RunSpec& run : runs) {
            const auto length = minimalBytes(static_cast<std::int64_t>(run.length), false);
            const auto delta = run.lcn < 0 ? std::vector<std::byte>{} : minimalBytes(run.lcn - previous, true);
            out.push_back(static_cast<std::byte>(length.size() | (delta.size() << 4U)));
            out.insert(out.end(), length.begin(), length.end());
            out.insert(out.end(), delta.begin(), delta.end());
            previous = run.lcn < 0 ? previous : run.lcn;
        }
        out.push_back(std::byte{0});
        return out;
    }

    // UTF-16LE bytes of an ASCII or BMP string.
    inline std::vector<std::byte> utf16(const std::u16string& text) {
        std::vector<std::byte> out;
        for (const char16_t c : text) {
            out.push_back(static_cast<std::byte>(c & 0xFFU));
            out.push_back(static_cast<std::byte>(c >> 8U));
        }
        return out;
    }

    // One $ATTRIBUTE_LIST entry as bytes: which record holds a segment of an attribute.
    inline std::vector<std::byte> listEntry(core::ntfs::AttributeType type, std::uint64_t vcn, std::uint64_t reference,
                                            std::uint16_t id, const std::u16string& name) {
        std::vector<std::byte> e(26);
        const auto put = [&](std::size_t at, auto v) { std::memcpy(e.data() + at, &v, sizeof(v)); };
        put(0, static_cast<std::uint32_t>(type));
        e[6] = static_cast<std::byte>(name.size());
        e[7] = static_cast<std::byte>(26);
        put(8, vcn);
        put(16, reference);
        put(24, id);
        const auto text = utf16(name);
        e.insert(e.end(), text.begin(), text.end());
        e.resize((e.size() + 7) / 8 * 8);
        put(4, static_cast<std::uint16_t>(e.size()));
        return e;
    }

    // Several attribute list entries back to back.
    inline std::vector<std::byte> joinBytes(const std::vector<std::vector<std::byte>>& parts) {
        std::vector<std::byte> out;
        for (const auto& part : parts) {
            out.insert(out.end(), part.begin(), part.end());
        }
        return out;
    }

    // Assembles one record.
    class RecordBuilder {
    public:
        // A record with the given number, sequence and header flags.
        explicit RecordBuilder(std::uint64_t number, std::uint16_t sequence = 1,
                               std::uint16_t flags = core::ntfs::recordInUse, std::size_t size = 1024) :
            number_(number), sequence_(sequence), flags_(flags), size_(size) {}

        // Makes it an extension record of a base file.
        RecordBuilder& base(std::uint64_t reference) {
            base_ = reference;
            return *this;
        }

        // Adds $STANDARD_INFORMATION.
        RecordBuilder& standard(std::int64_t created, std::int64_t modified, std::int64_t accessed,
                                std::uint32_t attributes) {
            std::vector<std::byte> value(72);
            put(value, 0, created);
            put(value, 8, modified);
            put(value, 16, modified);
            put(value, 24, accessed);
            put(value, 32, attributes);
            return resident(core::ntfs::AttributeType::StandardInformation, u"", value);
        }

        // Adds a $FILE_NAME.
        RecordBuilder& fileName(std::uint64_t parent, const std::u16string& name,
                                core::ntfs::NameSpace space = core::ntfs::NameSpace::Win32AndDos) {
            std::vector<std::byte> value(66);
            put(value, 0, parent);
            value[64] = static_cast<std::byte>(name.size());
            value[65] = static_cast<std::byte>(space);
            const auto text = utf16(name);
            value.insert(value.end(), text.begin(), text.end());
            return resident(core::ntfs::AttributeType::FileName, u"", value);
        }

        // Adds a resident attribute with a value.
        RecordBuilder& resident(core::ntfs::AttributeType type, const std::u16string& name,
                                const std::vector<std::byte>& value, std::uint16_t flags = 0) {
            std::vector<std::byte> attr(24);
            const std::size_t nameOffset = attr.size();
            append(attr, utf16(name));
            const std::size_t valueOffset = align(attr);
            append(attr, value);
            align(attr);
            put(attr, 9, static_cast<std::uint8_t>(name.size()));
            put(attr, 10, static_cast<std::uint16_t>(nameOffset));
            put(attr, 12, flags);
            put(attr, 16, static_cast<std::uint32_t>(value.size()));
            put(attr, 20, static_cast<std::uint16_t>(valueOffset));
            return add(type, std::move(attr));
        }

        // Adds a non-resident attribute segment mapping [startVcn, lastVcn] with the given run list.
        RecordBuilder& nonResident(core::ntfs::AttributeType type, const std::u16string& name, std::uint64_t startVcn,
                                   std::uint64_t lastVcn, const std::vector<std::byte>& runList, std::uint64_t dataSize,
                                   std::uint16_t flags = 0, std::uint8_t unit = 0) {
            std::vector<std::byte> attr(unit != 0 ? 72 : 64);
            const std::size_t nameOffset = attr.size();
            append(attr, utf16(name));
            const std::size_t runsOffset = align(attr);
            append(attr, runList);
            align(attr);
            attr[8] = std::byte{1};
            put(attr, 9, static_cast<std::uint8_t>(name.size()));
            put(attr, 10, static_cast<std::uint16_t>(nameOffset));
            put(attr, 12, flags);
            put(attr, 16, startVcn);
            put(attr, 24, lastVcn);
            put(attr, 32, static_cast<std::uint16_t>(runsOffset));
            put(attr, 34, unit);
            put(attr, 40, (lastVcn + 1 - startVcn) * 4096);
            put(attr, 48, dataSize);
            put(attr, 56, dataSize);
            return add(type, std::move(attr));
        }

        // The record bytes as on disk: update sequence numbers at every block end, real values in the array.
        [[nodiscard]] std::vector<std::byte> build() const {
            std::vector<std::byte> raw(size_);
            constexpr std::size_t usaOffset = 0x30;
            const std::size_t blocks = size_ / 512;
            std::memcpy(raw.data(), "FILE", 4);
            put(raw, 4, static_cast<std::uint16_t>(usaOffset));
            put(raw, 6, static_cast<std::uint16_t>(blocks + 1));
            put(raw, 0x10, sequence_);
            put(raw, 0x12, static_cast<std::uint16_t>(1));
            const std::size_t first = (usaOffset + 2 * (blocks + 1) + 7) / 8 * 8;
            put(raw, 0x14, static_cast<std::uint16_t>(first));
            put(raw, 0x16, flags_);
            put(raw, 0x1C, static_cast<std::uint32_t>(size_));
            put(raw, 0x20, base_);
            put(raw, 0x2C, static_cast<std::uint32_t>(number_));
            std::size_t at = first;
            for (const auto& attr : attributes_) {
                std::memcpy(raw.data() + at, attr.data(), attr.size());
                at += attr.size();
            }
            put(raw, at, static_cast<std::uint32_t>(0xFFFF'FFFFU));
            put(raw, 0x18, static_cast<std::uint32_t>(at + 8));
            protect(raw, usaOffset, blocks);
            return raw;
        }

    private:
        // Writes a little-endian value.
        template <typename T>
        static void put(std::vector<std::byte>& bytes, std::size_t offset, T value) {
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        }

        // Appends bytes.
        static void append(std::vector<std::byte>& to, const std::vector<std::byte>& bytes) {
            to.insert(to.end(), bytes.begin(), bytes.end());
        }

        // Pads to 8 bytes and returns the new size.
        static std::size_t align(std::vector<std::byte>& bytes) {
            bytes.resize((bytes.size() + 7) / 8 * 8);
            return bytes.size();
        }

        // Completes an attribute header and stores it.
        RecordBuilder& add(core::ntfs::AttributeType type, std::vector<std::byte> attr) {
            put(attr, 0, static_cast<std::uint32_t>(type));
            put(attr, 4, static_cast<std::uint32_t>(attr.size()));
            put(attr, 14, nextId_++);
            attributes_.push_back(std::move(attr));
            return *this;
        }

        // Moves each block's last two bytes into the update sequence array and stamps the sequence number.
        static void protect(std::vector<std::byte>& raw, std::size_t usaOffset, std::size_t blocks) {
            constexpr std::uint16_t usn = 0x0007;
            put(raw, usaOffset, usn);
            for (std::size_t i = 0; i < blocks; ++i) {
                const std::size_t end = (i + 1) * 512 - 2;
                std::memcpy(raw.data() + usaOffset + 2 * (i + 1), raw.data() + end, 2);
                put(raw, end, usn);
            }
        }

        std::uint64_t number_; // Record number.
        std::uint16_t sequence_; // Sequence number.
        std::uint16_t flags_; // Header flags.
        std::size_t size_; // Record size.
        std::uint64_t base_ = 0; // Base reference for extension records.
        std::uint16_t nextId_ = 0; // Next attribute id.
        std::vector<std::vector<std::byte>> attributes_; // Attributes in order.
    };

} // namespace icd::test
