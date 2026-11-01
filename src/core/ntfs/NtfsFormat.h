#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// On-disk NTFS 3.x structures the raw MFT scanner reads (plan.md 6.3, 10.2). Plain constants and loads only: the
// parser in this directory is pure C++ so it can be tested on handcrafted and captured records without a volume.
namespace icd::core::ntfs {

    // Attribute type codes from $AttrDef; the scanner reads only some of them, the rest are kept for their clusters.
    enum class AttributeType : std::uint32_t {
        StandardInformation = 0x10, // Timestamps and DOS attributes.
        AttributeList = 0x20, // Where the attributes of a file spread over several records live.
        FileName = 0x30, // One name of the file together with its parent directory.
        ObjectId = 0x40, // Distributed link tracking id.
        SecurityDescriptor = 0x50, // Old per-file security (NTFS 1.x); $Secure holds it today.
        VolumeName = 0x60, // Only in $Volume.
        VolumeInformation = 0x70, // Only in $Volume.
        Data = 0x80, // A data stream; the unnamed one is the file content.
        IndexRoot = 0x90, // Root of a B+ tree index (directories: $I30), always resident.
        IndexAllocation = 0xA0, // Index blocks of a large directory, on disk.
        Bitmap = 0xB0, // In-use bits: of index blocks, and of $MFT records in record 0.
        ReparsePoint = 0xC0, // Symlink, junction, dedup or cloud placeholder data.
        EaInformation = 0xD0, // Extended attribute sizes.
        Ea = 0xE0, // Extended attributes (WSL metadata).
        LoggedUtilityStream = 0x100, // $EFS keys and transactional data.
        End = 0xFFFF'FFFFU, // Marks the end of a record's attribute list.
    };

    // "FILE" as a little-endian 32-bit value: the signature of an MFT record.
    inline constexpr std::uint32_t fileSignature = 0x454C'4946U;

    // "BAAD": the signature NTFS writes over a record it found damaged.
    inline constexpr std::uint32_t badSignature = 0x4441'4142U;

    // Stride of the update sequence (multi-sector) protection; 512 whatever the device sector size.
    inline constexpr std::size_t fixupStride = 512;

    // Record header flags.
    inline constexpr std::uint16_t recordInUse = 0x0001; // The record describes a live file.
    inline constexpr std::uint16_t recordDirectory = 0x0002; // The file has a $I30 name index: a directory.

    // Attribute header flags.
    inline constexpr std::uint16_t attributeCompressionMask = 0x00FF; // Any compression format.
    inline constexpr std::uint16_t attributeEncrypted = 0x4000; // EFS-encrypted stream.
    inline constexpr std::uint16_t attributeSparse = 0x8000; // Sparse stream: holes take no clusters.

    // DOS attribute bits of $STANDARD_INFORMATION (the same values as FILE_ATTRIBUTE_*).
    inline constexpr std::uint32_t dosSparse = 0x0200; // Sparse file.
    inline constexpr std::uint32_t dosReparsePoint = 0x0400; // Reparse point.
    inline constexpr std::uint32_t dosCompressed = 0x0800; // Compressed file or directory default.
    inline constexpr std::uint32_t dosEncrypted = 0x4000; // EFS-encrypted.

    // Record numbers with a fixed meaning.
    inline constexpr std::uint64_t mftRecord = 0; // $MFT itself: its $DATA maps every record.
    inline constexpr std::uint64_t rootRecord = 5; // The root directory.
    inline constexpr std::uint64_t firstUserRecord = 16; // Records below are system or reserved.

    // MFT record number of a file reference (the 16-bit sequence number in the top bits removed).
    [[nodiscard]] constexpr std::uint64_t recordOf(std::uint64_t reference) {
        return reference & 0x0000'FFFF'FFFF'FFFFULL;
    }

    // Sequence number of a file reference: it changes each time a record is reused, so stale references differ.
    [[nodiscard]] constexpr std::uint16_t sequenceOf(std::uint64_t reference) {
        return static_cast<std::uint16_t>(reference >> 48U);
    }

    // A file reference from a record number and a sequence number.
    [[nodiscard]] constexpr std::uint64_t makeReference(std::uint64_t record, std::uint16_t sequence) {
        return recordOf(record) | (std::uint64_t{sequence} << 48U);
    }

    // Whether [offset, offset + size) lies inside bytes, without overflow.
    [[nodiscard]] constexpr bool fits(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
        return offset <= bytes.size() && size <= bytes.size() - offset;
    }

    // Reads a little-endian value at offset (x64 is little-endian). The caller has checked the bounds with fits().
    template <typename T>
    [[nodiscard]] T load(std::span<const std::byte> bytes, std::size_t offset) {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

} // namespace icd::core::ntfs
