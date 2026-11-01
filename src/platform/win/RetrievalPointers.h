#pragma once

#include "platform/win/Handle.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace icd::platform {

    // One run of a stream as FSCTL_GET_RETRIEVAL_POINTERS reports it.
    struct RetrievedRun {
        std::uint64_t vcn = 0; // Position inside the stream, in clusters.
        std::int64_t lcn = -1; // First cluster on disk; -1 for a hole (sparse or compressed away).
        std::uint64_t length = 0; // Clusters.
    };

    // The runs of the stream behind `file`, in VCN order. Empty for resident and empty streams. Returns false
    // (with GetLastError() set) when the query failed for another reason.
    bool readRetrievalPointers(HANDLE file, std::vector<RetrievedRun>& runs);

    // Metadata of an open file from one handle (FileBasicInfo, FileStandardInfo, FileStreamInfo).
    struct FileFacts {
        std::uint32_t attributes = 0; // FILE_ATTRIBUTE_* bits.
        std::int64_t created = 0; // FILETIME ticks.
        std::int64_t modified = 0; // Last write.
        std::int64_t accessed = 0; // Last access.
        std::uint64_t size = 0; // End of file of the main stream.
        std::uint32_t links = 1; // Hard link count.
        std::vector<std::wstring> namedStreams; // Named $DATA streams (without ":" and ":$DATA").
    };

    // Reads the facts; false when the basic or standard information cannot be read.
    bool readFileFacts(HANDLE file, FileFacts& facts);

    // Opens a file by its NTFS file reference on the volume, for attribute and retrieval pointer queries only
    // (FILE_READ_ATTRIBUTES, reparse points not followed, sharing everything). Invalid on failure.
    UniqueHandle openFileById(HANDLE volume, std::uint64_t fileReference);

    // Opens a path the same way (named streams: "C:\dir\file:stream").
    UniqueHandle openForQuery(const std::wstring& path);

} // namespace icd::platform
