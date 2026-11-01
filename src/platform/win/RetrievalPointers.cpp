#include "platform/win/RetrievalPointers.h"

#include <winioctl.h>

#include <array>

namespace icd::platform {

    namespace {

        constexpr DWORD shareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        constexpr DWORD queryFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;

        // Appends the runs of one output buffer; returns the VCN after the last one.
        std::uint64_t appendRuns(const RETRIEVAL_POINTERS_BUFFER& buffer, std::vector<RetrievedRun>& runs) {
            auto vcn = static_cast<std::uint64_t>(buffer.StartingVcn.QuadPart);
            for (DWORD i = 0; i < buffer.ExtentCount; ++i) {
                const auto next = static_cast<std::uint64_t>(buffer.Extents[i].NextVcn.QuadPart);
                runs.push_back({vcn, buffer.Extents[i].Lcn.QuadPart, next - vcn});
                vcn = next;
            }
            return vcn;
        }

        // Named $DATA streams from FILE_STREAM_INFO entries ("::$DATA" is the main stream).
        void readStreams(HANDLE file, FileFacts& facts) {
            alignas(8) std::array<std::byte, std::size_t{16} * 1024> buffer{};
            if (GetFileInformationByHandleEx(file, FileStreamInfo, buffer.data(), static_cast<DWORD>(buffer.size())) ==
                FALSE) {
                return; // Directories without streams, or no stream information: nothing named.
            }
            std::size_t offset = 0;
            for (;;) {
                const auto* info = reinterpret_cast<const FILE_STREAM_INFO*>(buffer.data() + offset);
                const std::wstring name(info->StreamName, info->StreamNameLength / sizeof(wchar_t));
                constexpr std::wstring_view suffix = L":$DATA";
                if (name != L"::$DATA" && name.size() > suffix.size() + 1 && name.ends_with(suffix)) {
                    facts.namedStreams.push_back(name.substr(1, name.size() - suffix.size() - 1));
                }
                if (info->NextEntryOffset == 0) {
                    return;
                }
                offset += info->NextEntryOffset;
            }
        }

    } // namespace

    bool readRetrievalPointers(HANDLE file, std::vector<RetrievedRun>& runs) {
        runs.clear();
        alignas(8) std::array<std::byte, std::size_t{64} * 1024> buffer{};
        STARTING_VCN_INPUT_BUFFER in{};
        for (;;) {
            DWORD bytes = 0;
            const BOOL ok = DeviceIoControl(file, FSCTL_GET_RETRIEVAL_POINTERS, &in, sizeof(in), buffer.data(),
                                            static_cast<DWORD>(buffer.size()), &bytes, nullptr);
            const DWORD error = ok != FALSE ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_HANDLE_EOF) {
                return true; // Resident or empty: no clusters.
            }
            if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) {
                return false;
            }
            const auto& out = *reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(buffer.data());
            const std::uint64_t next = appendRuns(out, runs);
            if (error == ERROR_SUCCESS) {
                return true;
            }
            in.StartingVcn.QuadPart = static_cast<LONGLONG>(next);
        }
    }

    bool readFileFacts(HANDLE file, FileFacts& facts) {
        FILE_BASIC_INFO basic{};
        FILE_STANDARD_INFO standard{};
        if (GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) == FALSE ||
            GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) == FALSE) {
            return false;
        }
        facts.attributes = basic.FileAttributes;
        facts.created = basic.CreationTime.QuadPart;
        facts.modified = basic.LastWriteTime.QuadPart;
        facts.accessed = basic.LastAccessTime.QuadPart;
        facts.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
        facts.links = standard.NumberOfLinks;
        facts.namedStreams.clear();
        readStreams(file, facts);
        return true;
    }

    UniqueHandle openFileById(HANDLE volume, std::uint64_t fileReference) {
        FILE_ID_DESCRIPTOR id{};
        id.dwSize = sizeof(id);
        id.Type = FileIdType;
        id.FileId.QuadPart = static_cast<LONGLONG>(fileReference);
        return UniqueHandle(OpenFileById(volume, &id, FILE_READ_ATTRIBUTES, shareAll, nullptr, queryFlags));
    }

    UniqueHandle openForQuery(const std::wstring& path) {
        return UniqueHandle(
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, shareAll, nullptr, OPEN_EXISTING, queryFlags, nullptr));
    }

} // namespace icd::platform
