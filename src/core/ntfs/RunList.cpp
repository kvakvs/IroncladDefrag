#include "core/ntfs/RunList.h"

#include <limits>
#include <optional>

namespace icd::core::ntfs {

    namespace {

        // One decoded mapping pair before it is placed.
        struct Pair {
            std::uint64_t length = 0; // Clusters.
            std::int64_t delta = 0; // LCN change from the previous run.
            bool hole = false; // No delta: the run is a hole.
            std::size_t size = 0; // Bytes the pair takes, header included.
        };

        // Little-endian unsigned integer of `size` bytes.
        std::uint64_t readUnsigned(const std::byte* bytes, unsigned size) {
            std::uint64_t value = 0;
            for (unsigned i = 0; i < size; ++i) {
                value |= std::uint64_t{static_cast<std::uint8_t>(bytes[i])} << (8U * i);
            }
            return value;
        }

        // Little-endian two's complement integer of `size` bytes (1-8), sign-extended.
        std::int64_t readSigned(const std::byte* bytes, unsigned size) {
            std::uint64_t value = readUnsigned(bytes, size);
            const bool negative = (static_cast<std::uint8_t>(bytes[size - 1]) & 0x80U) != 0;
            if (negative && size < 8) {
                value |= ~std::uint64_t{0} << (8U * size);
            }
            return static_cast<std::int64_t>(value);
        }

        // The pair starting at pos (not the terminator), or nullopt when it is malformed or cut off.
        std::optional<Pair> readPair(std::span<const std::byte> bytes, std::size_t pos) {
            const auto header = static_cast<std::uint8_t>(bytes[pos]);
            const unsigned lengthSize = header & 0x0FU;
            const unsigned deltaSize = header >> 4U;
            const std::size_t size = 1 + lengthSize + deltaSize;
            if (lengthSize == 0 || lengthSize > 8 || deltaSize > 8 || size > bytes.size() - pos) {
                return std::nullopt;
            }
            const std::byte* at = bytes.data() + pos + 1;
            const std::uint64_t length = readUnsigned(at, lengthSize);
            if (length == 0 || length > std::uint64_t{std::numeric_limits<std::int64_t>::max()}) {
                return std::nullopt;
            }
            const std::int64_t delta = deltaSize == 0 ? 0 : readSigned(at + lengthSize, deltaSize);
            return Pair{length, delta, deltaSize == 0, size};
        }

        // The LCN after adding delta, or nullopt when it would leave [0, int64 max].
        std::optional<std::int64_t> moveLcn(std::int64_t lcn, std::int64_t delta) {
            if (delta > 0 && lcn > std::numeric_limits<std::int64_t>::max() - delta) {
                return std::nullopt;
            }
            const std::int64_t next = lcn + delta; // lcn >= 0, so a negative delta cannot overflow.
            return next < 0 ? std::nullopt : std::optional(next);
        }

        // Where decoding stands: the next pair, its VCN and the LCN deltas are relative to.
        struct Cursor {
            std::size_t pos = 0; // Offset of the next pair.
            std::uint64_t vcn = 0; // VCN of the next run.
            std::int64_t lcn = 0; // LCN of the last run that was not a hole.
        };

        // Decodes the pair at the cursor into a run and advances; false when it is malformed or overflows.
        bool step(std::span<const std::byte> bytes, Cursor& at, std::vector<DataRun>& out) {
            const auto pair = readPair(bytes, at.pos);
            if (!pair || pair->length > std::numeric_limits<std::uint64_t>::max() - at.vcn) {
                return false;
            }
            const auto next = moveLcn(at.lcn, pair->delta);
            if (!next) {
                return false;
            }
            at.lcn = pair->hole ? at.lcn : *next;
            out.push_back({at.vcn, pair->hole ? -1 : at.lcn, pair->length});
            at.vcn += pair->length;
            at.pos += pair->size;
            return true;
        }

        // Decodes pairs until the terminator; false when one is malformed.
        bool decodePairs(std::span<const std::byte> bytes, std::uint64_t& vcn, std::vector<DataRun>& out) {
            Cursor at{.pos = 0, .vcn = vcn, .lcn = 0};
            while (at.pos < bytes.size() && bytes[at.pos] != std::byte{0}) {
                if (!step(bytes, at, out)) {
                    return false;
                }
            }
            vcn = at.vcn;
            return at.pos < bytes.size(); // Reached the terminator, not the end of the buffer.
        }

    } // namespace

    bool decodeRunList(std::span<const std::byte> bytes, std::uint64_t startVcn, std::uint64_t lastVcn,
                       std::vector<DataRun>& out) {
        const std::size_t original = out.size();
        std::uint64_t vcn = startVcn;
        // lastVcn + 1 wraps to startVcn for an empty attribute (lastVcn = startVcn - 1).
        const bool ok = decodePairs(bytes, vcn, out) && vcn - startVcn == lastVcn + 1 - startVcn;
        if (!ok) {
            out.resize(original);
        }
        return ok;
    }

} // namespace icd::core::ntfs
