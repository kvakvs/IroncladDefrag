#include "cli/commands/Classify.h"

#include "core/classify/Catalog.h"
#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "platform/win/Console.h"
#include "platform/win/FolderWalk.h"
#include "platform/win/TextConvert.h"
#include "services/exec/ThreadPool.h"
#include "services/plan/PlanJob.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace icd::cli {

    namespace {

        // What the command was asked for.
        struct ClassifyArgs {
            std::wstring folder; // Folder to walk.
            std::size_t depth = 2; // Folder depth below it that the per-class folders are grouped at.
            std::size_t top = 8; // Folders shown per class.
            std::string list; // Class id whose paths are listed instead; empty for the summary.
        };

        // A whole number argument, or nullopt.
        std::optional<std::size_t> numberOf(std::wstring_view text) {
            const std::string narrow = platform::toUtf8(std::wstring(text));
            std::size_t n = 0;
            const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), n);
            return error == std::errc() && end == narrow.data() + narrow.size() ? std::optional(n) : std::nullopt;
        }

        // One "--name value" pair of the command line.
        struct Option {
            std::wstring_view name; // "--depth", "--top" or "--list".
            std::wstring_view value; // Its value.
        };

        // Applies one option with its value; false when it is not known or the value is bad.
        bool applyOption(Option o, ClassifyArgs& out) {
            const std::wstring_view option = o.name;
            const std::wstring_view value = o.value;
            if (option == L"--list") {
                out.list = platform::toUtf8(std::wstring(value));
                return core::classify::builtinCatalog().find(out.list).has_value();
            }
            const auto n = numberOf(value);
            if (!n || (option != L"--depth" && option != L"--top")) {
                return false;
            }
            (option == L"--depth" ? out.depth : out.top) = *n;
            return true;
        }

        // The arguments, or nullopt when they are bad.
        std::optional<ClassifyArgs> argsOf(std::span<const std::wstring_view> args) {
            if (args.empty() || args[0].starts_with(L"--") || args.size() % 2 == 0) {
                return std::nullopt;
            }
            ClassifyArgs out;
            out.folder = std::wstring(args[0]);
            for (std::size_t i = 1; i + 1 < args.size(); i += 2) {
                if (!applyOption({.name = args[i], .value = args[i + 1]}, out)) {
                    return std::nullopt;
                }
            }
            return out;
        }

        // A walked folder as a snapshot without extents: the volume root, the folders down to the walked one, then
        // the walk's entries in order, so paths read as on the volume ("\Projects\app\src\main.cpp").
        struct FolderSnapshot {
            core::Snapshot snapshot; // Files only; nothing on disk.
            core::FileIndex top = 0; // The walked folder.
            core::FileIndex first = 0; // The walk's first entry; entry i is first + i.
        };

        // The snapshot of a walk.
        FolderSnapshot snapshotOf(const std::wstring& folder, const platform::FolderWalk& walk) {
            core::SnapshotBuilder b({.bytesPerCluster = 4096, .totalClusters = 1});
            core::FileIndex at = b.addFile(
                {.name = "", .parent = core::noFile, .sizeBytes = 0, .flags = core::FileDirectory, .times = {}});
            for (const auto& part : std::filesystem::absolute(folder).relative_path()) {
                if (!part.empty()) {
                    const std::string name = platform::toUtf8(part.wstring());
                    at = b.addFile(
                        {.name = name, .parent = at, .sizeBytes = 0, .flags = core::FileDirectory, .times = {}});
                }
            }
            const core::FileIndex first = at + 1;
            for (const platform::FolderEntry& e : walk.entries) {
                const auto flags = static_cast<std::uint16_t>((e.directory ? core::FileDirectory : 0) |
                                                              (e.reparsePoint ? core::FileReparsePoint : 0));
                b.addFile(
                    {.name = e.name,
                     .parent = e.parent == platform::walkRoot ? at : first + static_cast<core::FileIndex>(e.parent),
                     .sizeBytes = e.sizeBytes,
                     .flags = flags,
                     .times = {.created = 0, .modified = e.modified, .accessed = 0}});
            }
            return {.snapshot = std::move(b).build(), .top = at, .first = first};
        }

        // Per entry: the folder it is counted under, the ancestor `depth` levels below the walked folder (or its own
        // folder when that is shallower); walkRoot for files directly in the walked folder.
        std::vector<std::size_t> groupsOf(const platform::FolderWalk& walk, std::size_t depth) {
            const auto& entries = walk.entries;
            std::vector<std::size_t> level(entries.size(), 1);
            std::vector<std::size_t> folderGroup(entries.size(), platform::walkRoot);
            std::vector<std::size_t> out(entries.size(), platform::walkRoot);
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::size_t parent = entries[i].parent;
                level[i] = parent == platform::walkRoot ? 1 : level[parent] + 1;
                folderGroup[i] = level[i] > depth && parent != platform::walkRoot ? folderGroup[parent] : i;
                out[i] = parent == platform::walkRoot ? platform::walkRoot : folderGroup[parent];
            }
            return out;
        }

        // Files and bytes.
        struct Tally {
            std::uint64_t files = 0; // Regular files.
            std::uint64_t bytes = 0; // Their logical size.
        };

        // The rows of one class: per group folder its files and bytes.
        using GroupTallies = std::map<std::size_t, Tally>;

        // Files and bytes per class, and per class per group folder.
        struct ClassTallies {
            std::vector<Tally> classes; // By ClassId.
            std::vector<GroupTallies> groups; // By ClassId.
        };

        // Counts the regular files of a walk by class and group folder.
        ClassTallies tally(const FolderSnapshot& fs, const std::vector<std::uint8_t>& classes,
                           const std::vector<std::size_t>& groups) {
            const std::size_t count = core::classify::builtinCatalog().classes.size();
            ClassTallies out{.classes = std::vector<Tally>(count), .groups = std::vector<GroupTallies>(count)};
            for (std::size_t i = 0; i < groups.size(); ++i) {
                const core::FileIndex f = fs.first + static_cast<core::FileIndex>(i);
                if (fs.snapshot.files.has(f, core::FileDirectory)) {
                    continue;
                }
                const std::uint64_t size = fs.snapshot.files.sizeBytes(f);
                Tally& c = out.classes[classes[f]];
                Tally& g = out.groups[classes[f]][groups[i]];
                c = {.files = c.files + 1, .bytes = c.bytes + size};
                g = {.files = g.files + 1, .bytes = g.bytes + size};
            }
            return out;
        }

        // The path of a group folder as the report prints it.
        std::string groupPath(const FolderSnapshot& fs, std::size_t group) {
            const core::FileIndex f =
                group == platform::walkRoot ? fs.top : fs.first + static_cast<core::FileIndex>(group);
            return fs.snapshot.path(f);
        }

        // One class's line and its largest folders.
        void printClass(const FolderSnapshot& fs, const core::classify::ClassInfo& info, const Tally& total,
                        const GroupTallies& groups, std::size_t top) {
            std::printf("%-24s %12s files %12s\n", info.name.c_str(), core::formatCount(total.files).c_str(),
                        core::formatBytes(total.bytes).c_str());
            std::vector<std::pair<std::size_t, Tally>> rows(groups.begin(), groups.end());
            std::ranges::sort(rows, [](const auto& a, const auto& b) {
                return a.second.bytes != b.second.bytes ? a.second.bytes > b.second.bytes
                                                        : a.second.files > b.second.files;
            });
            for (std::size_t i = 0; i < std::min(top, rows.size()); ++i) {
                std::printf("    %12s files %12s  %s\n", core::formatCount(rows[i].second.files).c_str(),
                            core::formatBytes(rows[i].second.bytes).c_str(), groupPath(fs, rows[i].first).c_str());
            }
        }

        // The summary: totals, then every class with files and its largest folders.
        void printSummary(const FolderSnapshot& fs, const platform::FolderWalk& walk, const ClassTallies& t,
                          std::size_t top) {
            Tally all;
            for (const Tally& c : t.classes) {
                all = {.files = all.files + c.files, .bytes = all.bytes + c.bytes};
            }
            std::printf("%s: %s files, %s; %s folders could not be listed\n", fs.snapshot.path(fs.top).c_str(),
                        core::formatCount(all.files).c_str(), core::formatBytes(all.bytes).c_str(),
                        core::formatCount(walk.unreadable).c_str());
            const auto& catalog = core::classify::builtinCatalog().classes;
            for (std::size_t c = 0; c < catalog.size(); ++c) {
                if (t.classes[c].files > 0) {
                    printClass(fs, catalog[c], t.classes[c], t.groups[c], top);
                }
            }
        }

        // Every regular file of one class, one path per line.
        void printList(const FolderSnapshot& fs, const std::vector<std::uint8_t>& classes, core::classify::ClassId id) {
            for (core::FileIndex f = fs.first; f < fs.snapshot.files.size(); ++f) {
                if (classes[f] == id && !fs.snapshot.files.has(f, core::FileDirectory)) {
                    std::printf("%s\n", fs.snapshot.path(f).c_str());
                }
            }
        }

        // Walks, classifies and prints.
        void classifyFolder(const ClassifyArgs& args) {
            platform::useUtf8ConsoleOutput();
            const platform::FolderWalk walk = platform::walkFolder(args.folder);
            const FolderSnapshot fs = snapshotOf(args.folder, walk);
            services::ThreadPool pool(std::max(1U, std::thread::hardware_concurrency()));
            const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
            const auto classes = engine.classify(fs.snapshot, {.now = services::fileTimeNow(), .churn = {}}, pool);
            if (!args.list.empty()) {
                printList(fs, classes, *core::classify::builtinCatalog().find(args.list));
                return;
            }
            printSummary(fs, walk, tally(fs, classes, groupsOf(walk, args.depth)), args.top);
        }

    } // namespace

    const char* classifyUsage() { return "icd-cli classify <folder> [--depth N] [--top N] [--list CLASS]"; }

    int runClassify(std::span<const std::wstring_view> args) {
        const auto parsed = argsOf(args);
        if (!parsed) {
            std::fprintf(stderr, "Usage: %s\n", classifyUsage());
            return 2;
        }
        try {
            classifyFolder(*parsed);
            return 0;
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli classify: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
