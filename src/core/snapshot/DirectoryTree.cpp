#include "core/snapshot/DirectoryTree.h"

#include <format>
#include <numeric>

namespace icd::core {

    namespace {

        // Deepest path walked before assuming a cycle; NTFS paths are limited to about 32 K characters, so
        // real trees are far shallower.
        constexpr std::size_t maxDepth = 16384;

        // Whether file has no parent (noFile, or itself as NTFS stores the root).
        bool isRoot(const FileTable& files, FileIndex file) {
            const FileIndex parent = files.parent(file);
            return parent == noFile || parent == file;
        }

    } // namespace

    DirectoryTree::DirectoryTree(const FileTable& files) : childStart_(files.size() + 1, 0) {
        const auto count = static_cast<FileIndex>(files.size());
        for (FileIndex file = 0; file < count; ++file) {
            const FileIndex parent = files.parent(file);
            if (isRoot(files, file)) {
                roots_.push_back(file);
            }
            else if (parent >= count) {
                throw SnapshotError(std::format("file {} has parent {} outside the table", file, parent));
            }
            else {
                ++childStart_[parent + 1];
            }
        }
        std::partial_sum(childStart_.begin(), childStart_.end(), childStart_.begin());
        children_.resize(childStart_.back());
        std::vector<std::uint32_t> next(childStart_.begin(), childStart_.end() - 1);
        for (FileIndex file = 0; file < count; ++file) {
            if (!isRoot(files, file)) {
                children_[next[files.parent(file)]++] = file;
            }
        }
    }

    std::span<const FileIndex> DirectoryTree::children(FileIndex file) const {
        if (file + std::size_t{1} >= childStart_.size()) {
            return {};
        }
        const std::uint32_t begin = childStart_[file];
        return std::span<const FileIndex>(children_).subspan(begin, childStart_[file + 1] - begin);
    }

    std::string DirectoryTree::path(FileIndex file, const FileTable& files, const NamePool& names) {
        std::vector<FileIndex> chain;
        FileIndex current = file;
        while (!isRoot(files, current)) {
            if (chain.size() == maxDepth) {
                chain.push_back(noFile); // Marks the cut.
                break;
            }
            chain.push_back(current);
            current = files.parent(current);
        }
        if (chain.empty()) {
            return "\\";
        }
        std::string out;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            out += *it == noFile ? std::string("\\...") : "\\" + std::string(names.name(files.name(*it)));
        }
        return out;
    }

} // namespace icd::core
