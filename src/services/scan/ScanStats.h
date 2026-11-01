#pragma once

#include "core/snapshot/ScanReconcile.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace icd::services {

    // Which scanner read the files (plan.md 10.2): the raw MFT path is the default, the API path the reference and
    // the fallback.
    enum class ScannerKind : std::uint8_t {
        Api, // FSCTL_ENUM_USN_DATA plus per-file queries.
        Mft, // Raw $MFT read and parsed in parallel.
    };

    // "api" or "mft", for logs, reports and the command line.
    [[nodiscard]] const char* scannerName(ScannerKind kind);

    // What the raw MFT read cost and found, for the plan.md 6.5 targets (disk 90% busy) and the log.
    struct MftReadFigures {
        std::uint64_t records = 0; // Records $MFT holds.
        std::uint64_t recordsInUse = 0; // Records parsed that were in use.
        std::uint64_t skippedRecords = 0; // Not read: unused per $MFT::$BITMAP.
        std::uint64_t badRecords = 0; // Read but damaged (signature, fixups, header, attributes); skipped.
        std::uint64_t extensionRecords = 0; // In-use records holding attributes of another file.
        std::uint64_t attributeListFiles = 0; // Files whose attributes spread over several records.
        std::uint64_t extensionsJoined = 0; // Extension records their base file's attribute list names.
        std::uint64_t extensionsRejected = 0; // Extension records no current list names (stale); not used.
        std::uint64_t badAttributeLists = 0; // Attribute lists that could not be read or parsed.
        std::uint64_t bytesRead = 0; // Bytes read from the volume.
        std::uint64_t reads = 0; // Read requests.
        double readSeconds = 0.0; // Wall time of the read stream.
        double readerBusyPercent = 0.0; // Time with a read outstanding, as the reader saw it.
        double deviceBusyPercent = -1.0; // From the device counters; -1 when the device keeps none.
        double starvedSeconds = 0.0; // Time the reader waited for parser workers to free a buffer.
        std::uint32_t workers = 0; // Parser workers.
        std::uint32_t bufferBytes = 0; // Bytes per read buffer.
        std::uint32_t inFlight = 0; // Reads kept outstanding.
    };

    // What a scan saw, beyond the snapshot itself, for the summary and the log.
    struct ScanStats {
        ScannerKind scanner = ScannerKind::Api; // Who produced the snapshot.
        std::uint64_t files = 0; // Files and directories in the snapshot (the placeholder included).
        std::uint64_t directories = 0; // Directories among them.
        std::uint64_t streams = 0; // Named data streams read.
        std::uint64_t unreadable = 0; // Files that could not be opened or parsed (listed without extents).
        std::vector<core::FileIndex> unreadableFiles; // All of them: the log and the scanner cross-check need them.
        core::ReconcileStats reconcile; // Corrections for changes during the scan and unreadable areas.
        std::optional<MftReadFigures> mft; // Raw read figures (MFT scanner only).
    };

} // namespace icd::services
