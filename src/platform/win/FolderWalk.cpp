#include "platform/win/FolderWalk.h"

#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <windows.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace icd::platform {

    namespace {

        // Closes a FindFirstFileExW handle.
        struct FindCloser {
            void operator()(HANDLE h) const { FindClose(h); }
        };

        // An open directory listing.
        using FindHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, FindCloser>;

        // One folder still to be listed.
        struct Pending {
            std::wstring path; // Full path without a trailing backslash.
            std::size_t index = walkRoot; // Its entry, or walkRoot for the walked folder.
        };

        // Whether a found name is "." or "..".
        bool isDotName(const wchar_t* name) {
            return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
        }

        // The entry of one found item.
        FolderEntry entryOf(const WIN32_FIND_DATAW& data, std::size_t parent) {
            const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const std::uint64_t size = (std::uint64_t{data.nFileSizeHigh} << 32U) | data.nFileSizeLow;
            const std::uint64_t written =
                (std::uint64_t{data.ftLastWriteTime.dwHighDateTime} << 32U) | data.ftLastWriteTime.dwLowDateTime;
            return {.name = toUtf8(data.cFileName),
                    .parent = parent,
                    .sizeBytes = directory ? 0 : size,
                    .modified = static_cast<std::int64_t>(written),
                    .directory = directory,
                    .reparsePoint = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0};
        }

        // Starts listing a folder; a null handle when it cannot be listed.
        FindHandle openListing(const std::wstring& path, WIN32_FIND_DATAW& data) {
            const std::wstring pattern = path + L"\\*";
            HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                        FIND_FIRST_EX_LARGE_FETCH);
            return FindHandle(h == INVALID_HANDLE_VALUE ? nullptr : h);
        }

        // Lists one folder into the walk and queues its plain subfolders. False when it cannot be listed.
        bool listOne(const Pending& folder, FolderWalk& walk, std::vector<Pending>& stack) {
            WIN32_FIND_DATAW data{};
            const FindHandle listing = openListing(folder.path, data);
            if (!listing) {
                return false;
            }
            do {
                if (isDotName(data.cFileName)) {
                    continue;
                }
                walk.entries.push_back(entryOf(data, folder.index));
                const FolderEntry& e = walk.entries.back();
                if (e.directory && !e.reparsePoint) {
                    stack.push_back({folder.path + L"\\" + data.cFileName, walk.entries.size() - 1});
                }
            }
            while (FindNextFileW(listing.get(), &data) != FALSE);
            return true;
        }

    } // namespace

    FolderWalk walkFolder(const std::wstring& folder, const core::CancelToken& token) {
        std::wstring root = folder;
        while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) {
            root.pop_back();
        }
        if (root.size() == 3 && root.back() == L'\\') {
            root.pop_back(); // "T:\" lists as "T:\*".
        }
        FolderWalk walk;
        std::vector<Pending> stack;
        if (!listOne({root, walkRoot}, walk, stack)) {
            throw std::runtime_error("cannot list " + toUtf8(folder) + ": " + toUtf8(lastWinError()));
        }
        while (!stack.empty()) {
            token.throwIfCancelled();
            const Pending next = std::move(stack.back());
            stack.pop_back();
            walk.unreadable += listOne(next, walk, stack) ? 0 : 1;
        }
        return walk;
    }

} // namespace icd::platform
