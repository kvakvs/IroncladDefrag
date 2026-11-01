#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "core/model/VolumeLayout.h"
#include "core/snapshot/Snapshot.h"
#include "services/scan/MftMerge.h"
#include "services/scan/ScanStats.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace icd::services {

    // Reader settings of the raw MFT scan (plan.md 6.2 principle 1).
    struct MftScanOptions {
        std::uint32_t bufferBytes = 16U << 20U; // Bytes per read (8-16 MB).
        std::uint32_t inFlight = 3; // Reads outstanding (2-4).
        std::uint32_t minSkipBytes = 1U << 20U; // Unused $MFT stretches at least this long are not read.
    };

    // The snapshot and what it took.
    struct MftScanResult {
        core::Snapshot snapshot; // Files, extents and free space.
        ScanStats stats; // Counts and read figures (stats.mft).
    };

    // The fast scanner of plan.md 6.3 and 10.2: $MFT's own record gives the map of $MFT, $MFT::$BITMAP says which
    // records are in use, one reader thread reads the used ranges in volume order in large overlapped requests, and
    // parser tasks on `executor` turn each buffer into rows while the next reads are in flight (a bounded buffer pool
    // is the queue). The rows are merged into a snapshot with the same rules as the API path. Reads only.
    //   volume      handle with read access, for the USN journal cursor
    //   volumeName  "\\?\Volume{GUID}\" or "T:\", opened again for overlapped unbuffered reads
    //   ntfs        MFT location and record size (FSCTL_GET_NTFS_VOLUME_DATA)
    //   freeRuns    free space from the bitmap, read before
    //   partial     optional; gets a snapshot of the chunks parsed so far about every `partialInterval`, built on a
    //               pool task so the reader keeps the disk busy
    // Throws std::runtime_error when $MFT cannot be mapped or read (the caller may fall back to the API scanner) and
    // OperationCancelled on cancel.
    [[nodiscard]] MftScanResult scanFilesMft(HANDLE volume, const std::wstring& volumeName,
                                             core::VolumeGeometry geometry, const core::NtfsLayout& ntfs,
                                             std::vector<core::ClusterRun> freeRuns, core::Executor& executor,
                                             const core::CancelToken& token, core::ProgressSink* progress,
                                             const MftScanOptions& options = {}, const PartialScanSink& partial = {},
                                             std::chrono::milliseconds partialInterval = std::chrono::seconds(3));

    // Chosen MFT records read again (USN catch-up), with the extension records their attribute lists name.
    struct MftRecordSet {
        std::unique_ptr<mft::RowIndex> rows; // The chosen records that are in use, and their extension records.
        mft::ExtensionMap extensions; // Accepted extension records per base row.
        std::uint64_t recordCount = 0; // Records $MFT holds now.
        std::uint64_t bytesRead = 0; // Bytes read from the volume.
    };

    // Reads only the chosen records through the same pipeline as scanFilesMft() (planned reads of the stretches that
    // hold them, parser tasks), then, in a second round, the extension records their attribute lists name. Records
    // past the end of $MFT or not in use are left out. Throws like scanFilesMft().
    [[nodiscard]] MftRecordSet readMftRecords(const std::wstring& volumeName, core::VolumeGeometry geometry,
                                              const core::NtfsLayout& ntfs, const std::vector<std::uint64_t>& records,
                                              core::Executor& executor, const core::CancelToken& token);

} // namespace icd::services
