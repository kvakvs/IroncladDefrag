#include "services/scan/Classification.h"

#include "core/classify/Catalog.h"

namespace icd::services {

    namespace {

        // Files per chunk of the parallel count.
        constexpr std::size_t fileGrain = 65536;

        // Files and clusters per class, one slot per possible ClassId.
        struct ClassCounts {
            std::vector<std::uint64_t> files = std::vector<std::uint64_t>(256, 0); // Files per class.
            std::vector<std::uint64_t> clusters = std::vector<std::uint64_t>(256, 0); // Allocated clusters per class.
        };

        // Adds the counts of b to a.
        ClassCounts plus(ClassCounts a, const ClassCounts& b) {
            for (std::size_t c = 0; c < a.files.size(); ++c) {
                a.files[c] += b.files[c];
                a.clusters[c] += b.clusters[c];
            }
            return a;
        }

    } // namespace

    Classification classifyVolume(const core::Snapshot& snapshot, std::span<const std::uint8_t> churn, std::int64_t now,
                                  core::Executor& executor, const core::CancelToken& token) {
        static const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        Classification out;
        out.classes = engine.classify(snapshot, {.now = now, .churn = churn}, executor, token);
        out.summary = classSummary(snapshot, out.classes, executor, token);
        return out;
    }

    std::vector<ClassSummary> classSummary(const core::Snapshot& snapshot, std::span<const std::uint8_t> classes,
                                           core::Executor& executor, const core::CancelToken& token) {
        const auto& files = snapshot.files;
        const ClassCounts counts = core::parallelReduce(
            executor, {files.size(), fileGrain}, ClassCounts{},
            [&](std::size_t begin, std::size_t end) {
                ClassCounts part;
                for (std::size_t f = begin; f < end && f < classes.size(); ++f) {
                    ++part.files[classes[f]];
                    part.clusters[classes[f]] += files.allocatedClusters(static_cast<core::FileIndex>(f));
                }
                return part;
            },
            plus, token);
        std::vector<ClassSummary> out;
        const auto& catalog = core::classify::builtinCatalog().classes;
        for (std::size_t c = 0; c < catalog.size(); ++c) {
            if (counts.files[c] != 0) {
                out.push_back({.id = catalog[c].id,
                               .name = catalog[c].name,
                               .files = counts.files[c],
                               .bytes = counts.clusters[c] * snapshot.geometry.bytesPerCluster,
                               .inZonePercent = std::nullopt});
            }
        }
        return out;
    }

} // namespace icd::services
