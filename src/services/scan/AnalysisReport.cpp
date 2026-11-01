#include "services/scan/AnalysisReport.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/DriveText.h"
#include "core/format/Json.h"
#include "core/snapshot/ExtentIndex.h"

#include <array>
#include <format>

namespace icd::services {

    namespace {

        using core::JsonWriter;

        // File flag names in bit order (FileFlag).
        constexpr std::array<std::pair<std::uint16_t, const char*>, 10> flagNames{{
            {core::FileDirectory, "directory"},
            {core::FileResident, "resident"},
            {core::FileCompressed, "compressed"},
            {core::FileSparse, "sparse"},
            {core::FileEncrypted, "encrypted"},
            {core::FileReparsePoint, "reparse-point"},
            {core::FileMetadata, "metadata"},
            {core::FileHardLinked, "hard-linked"},
            {core::FileAlternateStreams, "alternate-streams"},
            {core::FileUnmovable, "unmovable"},
        }};

        // NTFS facts of the volume.
        void writeNtfs(JsonWriter& json, const core::NtfsLayout& ntfs) {
            json.beginObject()
                .key("serialNumber")
                .value(std::format("0x{:016x}", ntfs.serialNumber))
                .key("version")
                .value(std::format("{}.{}", ntfs.majorVersion, ntfs.minorVersion))
                .key("bytesPerFileRecord")
                .value(std::uint64_t{ntfs.bytesPerFileRecord})
                .key("mftValidDataLength")
                .value(ntfs.mftValidDataLength)
                .key("mftStartLcn")
                .value(ntfs.mftStartLcn)
                .key("mft2StartLcn")
                .value(ntfs.mft2StartLcn)
                .key("mftZoneStart")
                .value(ntfs.mftZoneStart)
                .key("mftZoneEnd")
                .value(ntfs.mftZoneEnd)
                .key("totalReserved")
                .value(ntfs.totalReserved)
                .endObject();
        }

        // Where the volume sits and how it is cut into clusters.
        void writeLayout(JsonWriter& json, const core::DriveInfo& drive, const core::Snapshot& snapshot) {
            const core::VolumeLayout& layout = drive.layout;
            json.key("volumeName").value(layout.volumeName);
            json.key("bytesPerCluster").value(std::uint64_t{snapshot.geometry.bytesPerCluster});
            json.key("totalClusters").value(snapshot.geometry.totalClusters);
            json.key("diskNumber");
            layout.diskNumber ? json.value(std::uint64_t{*layout.diskNumber}) : json.null();
            json.key("partitionOffset").value(layout.partitionOffset);
            json.key("diskBytes").value(layout.diskBytes);
            json.key("ntfs");
            layout.ntfs ? writeNtfs(json, *layout.ntfs) : static_cast<void>(json.null());
        }

        // The volume object.
        void writeVolume(JsonWriter& json, const core::DriveInfo& drive, const core::Snapshot& snapshot) {
            json.key("volume").beginObject();
            json.key("root").value(drive.root);
            json.key("label").value(drive.label);
            json.key("fileSystem").value(drive.fileSystem);
            json.key("media").value(core::describeMedia(drive.media));
            json.key("bus").value(drive.storage.bus);
            json.key("model").value(drive.storage.model);
            json.key("totalBytes").value(snapshot.geometry.totalClusters * snapshot.geometry.bytesPerCluster);
            writeLayout(json, drive, snapshot);
            json.endObject();
        }

        // The raw read figures of the MFT scanner.
        void writeMftFigures(JsonWriter& json, const MftReadFigures& f) {
            json.key("mft").beginObject();
            json.key("records").value(f.records);
            json.key("recordsInUse").value(f.recordsInUse);
            json.key("skippedRecords").value(f.skippedRecords);
            json.key("badRecords").value(f.badRecords);
            json.key("extensionRecords").value(f.extensionRecords);
            json.key("attributeListFiles").value(f.attributeListFiles);
            json.key("extensionsJoined").value(f.extensionsJoined);
            json.key("extensionsRejected").value(f.extensionsRejected);
            json.key("badAttributeLists").value(f.badAttributeLists);
            json.key("bytesRead").value(f.bytesRead);
            json.key("reads").value(f.reads);
            json.key("readSeconds").value(f.readSeconds);
            json.key("readerBusyPercent").value(f.readerBusyPercent);
            json.key("deviceBusyPercent").value(f.deviceBusyPercent);
            json.key("starvedSeconds").value(f.starvedSeconds);
            json.key("workers").value(std::uint64_t{f.workers});
            json.key("bufferBytes").value(std::uint64_t{f.bufferBytes});
            json.key("inFlight").value(std::uint64_t{f.inFlight});
            json.endObject();
        }

        // The scan object: scanner, time, journal cursor and what had to be corrected.
        void writeScan(JsonWriter& json, const AnalysisResult& result) {
            json.key("scan").beginObject();
            json.key("scanner").value(scannerName(result.scan.scanner));
            json.key("seconds").value(result.seconds);
            json.key("scanSeconds").value(result.scanSeconds);
            json.key("caughtUp").value(result.caughtUp);
            json.key("savedSnapshot").value(result.catchUp);
            json.key("memoryPlan").value(result.memoryPlan);
            json.key("usnJournal");
            if (result.snapshot.usn) {
                json.beginObject()
                    .key("journalId")
                    .value(std::format("0x{:016x}", result.snapshot.usn->journalId))
                    .key("nextUsn")
                    .value(result.snapshot.usn->nextUsn)
                    .endObject();
            }
            else {
                json.null();
            }
            json.key("filesInSnapshot").value(std::uint64_t{result.snapshot.files.size()});
            json.key("extents").value(std::uint64_t{result.snapshot.extents.size()});
            json.key("namedStreams").value(result.scan.streams);
            json.key("unreadableFiles").value(result.scan.unreadable);
            json.key("changedDuringScan").value(result.scan.reconcile.droppedExtents);
            json.key("unattributedClusters").value(result.scan.reconcile.unattributed);
            if (result.scan.mft) {
                writeMftFigures(json, *result.scan.mft);
            }
            json.endObject();
        }

        // The summary object: exactly the figures of the GUI's Summary tab.
        void writeSummary(JsonWriter& json, const AnalysisSummary& summary) {
            json.key("summary").beginObject();
            json.key("fragmentationPercent").value(summary.fragmentationPercent);
            json.key("files").value(summary.files);
            json.key("fragmentedFiles").value(summary.fragmentedFiles);
            json.key("extraFragments").value(summary.fragments);
            json.key("freeBytes").value(summary.freeBytes);
            json.key("freeRuns").value(summary.freeRuns);
            json.key("largestFreeRunBytes").value(summary.largestFreeRun);
            if (summary.scan) {
                const AnalysisSummary::ScanFigures& scan = *summary.scan;
                json.key("directories").value(scan.directories);
                json.key("fragmentedBytesPercent").value(scan.fragmentedBytesPercent);
                json.key("largeRunFreePercent").value(scan.largeRunFreePercent);
                json.key("unmovableBytes").value(scan.unmovableBytes);
            }
            json.endObject();
        }

        // One file's runs as [lcn, clusters, stream], records that continue on disk joined.
        void writeRuns(JsonWriter& json, const core::Snapshot& snapshot, core::FileIndex file) {
            json.key("runs").beginArray();
            const auto records = snapshot.extents.records();
            const auto positions = snapshot.extents.extentsOf(file);
            for (std::size_t i = 0; i < positions.size();) {
                const core::ExtentRecord& first = records[positions[i]];
                core::ClusterCount length = first.length();
                std::size_t j = i + 1;
                for (; j < positions.size(); ++j) {
                    const core::ExtentRecord& next = records[positions[j]];
                    if (next.stream() != first.stream() || next.lcn() != first.lcn() + length) {
                        break;
                    }
                    length += next.length();
                }
                json.beginArray().value(first.lcn()).value(length).value(std::uint64_t{first.stream()}).endArray();
                i = j;
            }
            json.endArray();
        }

        // Every file that owns clusters, with path, size, flags and runs.
        void writeExtents(JsonWriter& json, const core::Snapshot& snapshot) {
            json.key("files").beginArray();
            for (core::FileIndex file = 0; file < snapshot.files.size(); ++file) {
                if (snapshot.extents.extentsOf(file).empty()) {
                    continue;
                }
                json.beginObject();
                json.key("path").value(snapshot.path(file));
                json.key("size").value(snapshot.files.sizeBytes(file));
                json.key("flags").beginArray();
                for (const std::string& name : fileFlagNames(snapshot.files.flags(file))) {
                    json.value(name);
                }
                json.endArray();
                writeRuns(json, snapshot, file);
                json.endObject();
            }
            json.endArray();
        }

    } // namespace

    std::vector<std::string> fileFlagNames(std::uint16_t flags) {
        std::vector<std::string> names;
        for (const auto& [flag, name] : flagNames) {
            if ((flags & flag) != 0) {
                names.emplace_back(name);
            }
        }
        return names;
    }

    std::string analysisJson(const core::DriveInfo& drive, const AnalysisResult& result, bool dumpExtents) {
        JsonWriter json;
        json.beginObject();
        writeVolume(json, drive, result.snapshot);
        writeScan(json, result);
        writeSummary(json, result.summary);
        if (dumpExtents) {
            writeExtents(json, result.snapshot);
        }
        json.endObject();
        return json.str() + "\n";
    }

    std::string analysisText(const core::DriveInfo& drive, const AnalysisResult& result) {
        const AnalysisSummary& s = result.summary;
        std::string text = std::format("{} ({}, {}) analysed in {:.1f} s ({} scanner, files in {:.2f} s)\n",
                                       core::driveName(drive), core::describeStorage(drive), drive.fileSystem,
                                       result.seconds, scannerName(result.scan.scanner), result.scanSeconds);
        if (!result.catchUp.empty()) {
            text += std::format("  Saved snapshot     {}\n", result.catchUp);
        }
        if (!result.fallbackReason.empty()) {
            text += std::format("  MFT scan failed, API scan used: {}\n", result.fallbackReason);
        }
        text += std::format("  Fragmentation      {:.1f}% of files ({} of {})\n", s.fragmentationPercent,
                            core::formatCount(s.fragmentedFiles), core::formatCount(s.files));
        text += std::format("  Extra fragments    {}\n", core::formatCount(s.fragments));
        text += std::format("  Free space         {} in {} runs, largest {}\n", core::formatBytes(s.freeBytes),
                            core::formatCount(s.freeRuns), core::formatBytes(s.largestFreeRun));
        if (s.scan) {
            text += std::format("  Directories        {}\n", core::formatCount(s.scan->directories));
            text += std::format("  Fragmented data    {:.1f}%\n", s.scan->fragmentedBytesPercent);
            text += std::format("  Free in 64 MB+     {:.1f}%\n", s.scan->largeRunFreePercent);
            text += std::format("  Unmovable          {}\n", core::formatBytes(s.scan->unmovableBytes));
        }
        text +=
            std::format("  Unreadable files   {}, changed during scan {}\n", core::formatCount(result.scan.unreadable),
                        core::formatCount(result.scan.reconcile.droppedExtents));
        return text;
    }

} // namespace icd::services
