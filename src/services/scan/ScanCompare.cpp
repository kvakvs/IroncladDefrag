#include "services/scan/ScanCompare.h"

#include "core/ntfs/NtfsFormat.h"
#include "services/scan/MftMerge.h"

#include <algorithm>
#include <format>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace icd::services {

    namespace {

        using core::FileIndex;
        using core::Snapshot;

        // Differences kept as text.
        constexpr std::size_t maxSamples = 50;

        // A stream's run as compared: stream ordinal, first cluster, length.
        using Run = std::tuple<std::uint16_t, core::Lcn, core::ClusterCount>;

        // Collects differences.
        class Report {
        public:
            explicit Report(ScanComparison& out) : out_(out) {}

            // Records one difference.
            void differ(std::string text) {
                ++out_.differences;
                if (out_.samples.size() < maxSamples) {
                    out_.samples.push_back(std::move(text));
                }
            }

            // Records a difference when two values are not equal.
            template <typename T>
            void expectEqual(const T& a, const T& b, const std::string& what) {
                if (!(a == b)) {
                    differ(what);
                }
            }

        private:
            ScanComparison& out_; // Being filled.
        };

        // A file's runs of data streams and directory indexes (ordinals below mft::extraStreamBase), in stream and VCN
        // order, joined where a run continues the previous one on disk (record splits and scanner-specific cuts
        // disappear).
        std::vector<Run> runsOf(const Snapshot& s, FileIndex f) {
            std::vector<Run> out;
            for (const std::uint32_t position : s.extents.extentsOf(f)) {
                const core::ExtentRecord& r = s.extents.records()[position];
                if (r.stream() >= mft::extraStreamBase) {
                    continue;
                }
                if (!out.empty() && std::get<0>(out.back()) == r.stream() &&
                    std::get<1>(out.back()) + std::get<2>(out.back()) == r.lcn()) {
                    std::get<2>(out.back()) += r.length();
                    continue;
                }
                out.emplace_back(r.stream(), r.lcn(), r.length());
            }
            return out;
        }

        // Clusters of a run list.
        core::ClusterCount clustersOf(const std::vector<Run>& runs) {
            core::ClusterCount total = 0;
            for (const Run& r : runs) {
                total += std::get<2>(r);
            }
            return total;
        }

        // Files by MFT record, the placeholder (last file) left out.
        std::unordered_map<std::uint64_t, FileIndex> byRecord(const Snapshot& s) {
            std::unordered_map<std::uint64_t, FileIndex> out;
            for (FileIndex f = 0; f + 1 < s.files.size(); ++f) {
                out.emplace(core::ntfs::recordOf(s.files.fileReference(f)), f);
            }
            return out;
        }

        // MFT record of a file's parent, or the "none" marker for roots.
        std::uint64_t parentRecord(const Snapshot& s, FileIndex f) {
            const FileIndex parent = s.files.parent(f);
            return parent == core::noFile ? ~std::uint64_t{0} : core::ntfs::recordOf(s.files.fileReference(parent));
        }

        // One readable file of the API scan against the MFT scan's file of the same record.
        class FilePair {
        public:
            FilePair(const Snapshot& api, FileIndex a, const Snapshot& mft, FileIndex m) :
                api_(api), mft_(mft), a_(a), m_(m), path_(api.path(a)) {}

            // Compares every field and the runs; returns the clusters compared.
            core::ClusterCount compare(Report& report, ScanComparison& out) const {
                fields(report);
                const auto apiRuns = runsOf(api_, a_);
                const auto mftRuns = runsOf(mft_, m_);
                out.runsCompared += apiRuns.size();
                if (apiRuns != mftRuns) {
                    report.differ(std::format("{}: {} runs ({} clusters) by API, {} runs ({} clusters) by MFT", path_,
                                              apiRuns.size(), clustersOf(apiRuns), mftRuns.size(),
                                              clustersOf(mftRuns)));
                }
                return clustersOf(apiRuns);
            }

        private:
            // Name, parent, size, flags and times.
            void fields(Report& report) const {
                const auto& af = api_.files;
                const auto& mf = mft_.files;
                report.expectEqual(api_.names.name(af.name(a_)), mft_.names.name(mf.name(m_)),
                                   std::format("{}: name differs (MFT: {})", path_, mft_.path(m_)));
                report.expectEqual(parentRecord(api_, a_), parentRecord(mft_, m_), path_ + ": parent differs");
                report.expectEqual(
                    af.sizeBytes(a_), mf.sizeBytes(m_),
                    std::format("{}: size {} by API, {} by MFT", path_, af.sizeBytes(a_), mf.sizeBytes(m_)));
                report.expectEqual(
                    af.flags(a_), mf.flags(m_),
                    std::format("{}: flags 0x{:x} by API, 0x{:x} by MFT", path_, af.flags(a_), mf.flags(m_)));
                const auto& at = af.times(a_);
                const auto& mt = mf.times(m_);
                report.expectEqual(std::tie(at.created, at.modified, at.accessed),
                                   std::tie(mt.created, mt.modified, mt.accessed), path_ + ": times differ");
            }

            const Snapshot& api_; // Reference scan.
            const Snapshot& mft_; // Scan under test.
            FileIndex a_; // File in the reference.
            FileIndex m_; // Same record in the scan under test.
            std::string path_; // For messages.
        };

        // Clusters neither free nor in compared runs.
        core::ClusterCount remainder(const Snapshot& s, core::ClusterCount compared) {
            return s.geometry.totalClusters - s.freeSpace.freeClusters() - compared;
        }

        // The free runs of both, as values.
        void compareFree(const Snapshot& a, const Snapshot& b, Report& report) {
            const auto ra = a.freeSpace.runs();
            const auto rb = b.freeSpace.runs();
            if (!std::ranges::equal(ra, rb)) {
                report.differ(std::format("free space: {} runs ({} clusters) vs {} runs ({} clusters)", ra.size(),
                                          a.freeSpace.freeClusters(), rb.size(), b.freeSpace.freeClusters()));
            }
        }

        // Every column of file f.
        bool sameFile(const Snapshot& a, const Snapshot& b, FileIndex f) {
            const auto& x = a.files;
            const auto& y = b.files;
            const auto& tx = x.times(f);
            const auto& ty = y.times(f);
            return a.names.name(x.name(f)) == b.names.name(y.name(f)) && x.parent(f) == y.parent(f) &&
                x.sizeBytes(f) == y.sizeBytes(f) && x.allocatedClusters(f) == y.allocatedClusters(f) &&
                x.flags(f) == y.flags(f) && x.fileReference(f) == y.fileReference(f) &&
                std::tie(tx.created, tx.modified, tx.accessed) == std::tie(ty.created, ty.modified, ty.accessed) &&
                std::ranges::equal(a.extents.extentsOf(f), b.extents.extentsOf(f));
        }

        // Every file the API path could read against the MFT scan; returns the clusters compared on each side.
        std::pair<core::ClusterCount, core::ClusterCount> compareReadable(const Snapshot& api,
                                                                          const ScanStats& apiStats,
                                                                          const Snapshot& mft, Report& report,
                                                                          ScanComparison& out) {
            const auto mftFiles = byRecord(mft);
            const std::unordered_set<FileIndex> unreadable(apiStats.unreadableFiles.begin(),
                                                           apiStats.unreadableFiles.end());
            core::ClusterCount apiCompared = 0;
            core::ClusterCount mftCompared = 0;
            for (FileIndex a = 0; a + 1 < api.files.size(); ++a) {
                const auto it = mftFiles.find(core::ntfs::recordOf(api.files.fileReference(a)));
                if (it == mftFiles.end()) {
                    report.differ(api.path(a) + ": missing from the MFT scan");
                }
                else if (!unreadable.contains(a)) {
                    ++out.filesCompared;
                    apiCompared += FilePair(api, a, mft, it->second).compare(report, out);
                    mftCompared += clustersOf(runsOf(mft, it->second));
                }
            }
            return {apiCompared, mftCompared};
        }

        // MFT files the API scan does not list. FSCTL_ENUM_USN_DATA leaves out some $Extend metadata ($Quota, $ObjId,
        // $Reparse, ...); the MFT path lists them, which only makes it more complete. Anything else is a difference.
        void countExtras(const std::unordered_map<std::uint64_t, FileIndex>& apiFiles, const Snapshot& mft,
                         Report& report, ScanComparison& out) {
            for (FileIndex m = 0; m + 1 < mft.files.size(); ++m) {
                if (apiFiles.contains(core::ntfs::recordOf(mft.files.fileReference(m)))) {
                    continue;
                }
                if (mft.files.has(m, core::FileMetadata)) {
                    ++out.extraMetadataFiles;
                }
                else {
                    report.differ(mft.path(m) + ": missing from the API scan");
                }
            }
        }

    } // namespace

    ScanComparison compareWithApi(const Snapshot& api, const ScanStats& apiStats, const Snapshot& mft) {
        ScanComparison out;
        Report report(out);
        const auto [apiCompared, mftCompared] = compareReadable(api, apiStats, mft, report, out);
        countExtras(byRecord(api), mft, report, out);
        report.expectEqual(api.files.size() + out.extraMetadataFiles, mft.files.size(),
                           std::format("file count: {} by API, {} by MFT", api.files.size(), mft.files.size()));
        compareFree(api, mft, report);
        report.expectEqual(remainder(api, apiCompared), remainder(mft, mftCompared),
                           std::format("clusters outside compared files: {} by API, {} by MFT",
                                       remainder(api, apiCompared), remainder(mft, mftCompared)));
        return out;
    }

    ScanComparison compareExact(const Snapshot& a, const Snapshot& b) {
        ScanComparison out;
        Report report(out);
        report.expectEqual(std::tie(a.geometry.bytesPerCluster, a.geometry.totalClusters),
                           std::tie(b.geometry.bytesPerCluster, b.geometry.totalClusters), "geometry differs");
        report.expectEqual(a.files.size(), b.files.size(),
                           std::format("file count: {} vs {}", a.files.size(), b.files.size()));
        const std::size_t files = std::min(a.files.size(), b.files.size());
        for (FileIndex f = 0; f < files; ++f) {
            ++out.filesCompared;
            if (!sameFile(a, b, f)) {
                report.differ(std::format("file {} ({}) differs", f, a.path(f)));
            }
        }
        out.runsCompared = a.extents.size();
        if (!std::ranges::equal(a.extents.records(), b.extents.records())) {
            report.differ(std::format("extent records differ ({} vs {})", a.extents.size(), b.extents.size()));
        }
        compareFree(a, b, report);
        return out;
    }

} // namespace icd::services
