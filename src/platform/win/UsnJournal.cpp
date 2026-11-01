#include "platform/win/UsnJournal.h"

#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <winioctl.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

namespace icd::platform {

    namespace {

        // Throws with the Win32 error text of the last call.
        [[noreturn]] void fail(const char* what) {
            throw std::runtime_error(std::string(what) + " failed: " + toUtf8(lastWinError()));
        }

        // One USN_RECORD_V2 as an entry; the name is UTF-16 inside the record.
        UsnFileEntry toEntry(const USN_RECORD_V2& record) {
            UsnFileEntry entry;
            entry.fileReference = record.FileReferenceNumber;
            entry.parentReference = record.ParentFileReferenceNumber;
            entry.attributes = record.FileAttributes;
            const auto* name =
                reinterpret_cast<const wchar_t*>(reinterpret_cast<const std::byte*>(&record) + record.FileNameOffset);
            entry.name = toUtf8(std::wstring(name, record.FileNameLength / sizeof(wchar_t)));
            return entry;
        }

        // Entries of one FSCTL_ENUM_USN_DATA output buffer (after the leading next-reference field).
        std::vector<UsnFileEntry> parseRecords(const std::vector<std::byte>& buffer, DWORD bytes) {
            std::vector<UsnFileEntry> entries;
            std::size_t offset = sizeof(USN);
            while (offset + sizeof(USN_RECORD_V2) <= bytes) {
                const auto* record = reinterpret_cast<const USN_RECORD_V2*>(buffer.data() + offset);
                if (record->RecordLength == 0 || offset + record->RecordLength > bytes) {
                    break;
                }
                if (record->MajorVersion == 2) {
                    entries.push_back(toEntry(*record));
                }
                offset += record->RecordLength;
            }
            return entries;
        }

        // Change records of one FSCTL_READ_USN_JOURNAL output buffer (after the leading next-USN field), below endUsn.
        std::vector<UsnChange> parseChanges(std::span<const std::byte> buffer, std::int64_t endUsn) {
            std::vector<UsnChange> changes;
            std::size_t offset = sizeof(USN);
            while (offset + sizeof(USN_RECORD_V2) <= buffer.size()) {
                const auto* record = reinterpret_cast<const USN_RECORD_V2*>(buffer.data() + offset);
                if (record->RecordLength == 0 || offset + record->RecordLength > buffer.size()) {
                    break;
                }
                if (record->MajorVersion == 2 && record->Usn < endUsn) {
                    changes.push_back(
                        {record->FileReferenceNumber, record->ParentFileReferenceNumber, record->Usn, record->Reason});
                }
                offset += record->RecordLength;
            }
            return changes;
        }

        // The status for a failed read, or nullopt when the failure is unexpected.
        std::optional<UsnReadStatus> statusOf(DWORD error) {
            if (error == ERROR_JOURNAL_ENTRY_DELETED) {
                return UsnReadStatus::RecordsLost;
            }
            if (error == ERROR_JOURNAL_NOT_ACTIVE || error == ERROR_JOURNAL_DELETE_IN_PROGRESS ||
                error == ERROR_INVALID_PARAMETER) {
                return UsnReadStatus::JournalChanged; // A wrong journal id is reported as an invalid parameter.
            }
            return std::nullopt;
        }

    } // namespace

    std::optional<UsnJournalState> queryUsnJournal(HANDLE volume) {
        USN_JOURNAL_DATA_V0 data{};
        DWORD bytes = 0;
        if (DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytes, nullptr) ==
            FALSE) {
            const DWORD error = GetLastError();
            if (error == ERROR_JOURNAL_NOT_ACTIVE || error == ERROR_JOURNAL_DELETE_IN_PROGRESS) {
                return std::nullopt;
            }
            fail("FSCTL_QUERY_USN_JOURNAL");
        }
        return UsnJournalState{.journalId = data.UsnJournalID,
                               .firstUsn = data.FirstUsn,
                               .nextUsn = data.NextUsn,
                               .lowestValidUsn = data.LowestValidUsn,
                               .maximumSize = data.MaximumSize};
    }

    void enumerateUsnFiles(HANDLE volume, const std::function<void(std::span<const UsnFileEntry>)>& onBatch,
                           const core::CancelToken& token) {
        MFT_ENUM_DATA_V0 in{};
        in.StartFileReferenceNumber = 0;
        in.LowUsn = 0;
        in.HighUsn = std::numeric_limits<USN>::max();
        std::vector<std::byte> buffer(std::size_t{64} * 1024);
        for (;;) {
            token.throwIfCancelled();
            DWORD bytes = 0;
            if (DeviceIoControl(volume, FSCTL_ENUM_USN_DATA, &in, sizeof(in), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE) {
                if (GetLastError() == ERROR_HANDLE_EOF) {
                    return; // Past the last record.
                }
                fail("FSCTL_ENUM_USN_DATA");
            }
            if (bytes < sizeof(USN)) {
                return;
            }
            const auto entries = parseRecords(buffer, bytes);
            if (!entries.empty()) {
                onBatch(entries);
            }
            std::memcpy(&in.StartFileReferenceNumber, buffer.data(), sizeof(USN)); // Where the next call starts.
        }
    }

    namespace {

        // One FSCTL_READ_USN_JOURNAL call into buffer; the bytes returned, or the status that ends the read.
        std::variant<DWORD, UsnReadStatus> readBatch(HANDLE volume, READ_USN_JOURNAL_DATA_V0& in,
                                                     std::vector<std::byte>& buffer) {
            DWORD bytes = 0;
            if (DeviceIoControl(volume, FSCTL_READ_USN_JOURNAL, &in, sizeof(in), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE) {
                if (const auto status = statusOf(GetLastError())) {
                    return *status;
                }
                fail("FSCTL_READ_USN_JOURNAL");
            }
            return bytes;
        }

    } // namespace

    UsnReadStatus readUsnJournal(HANDLE volume, const UsnRange& range,
                                 const std::function<void(std::span<const UsnChange>)>& onBatch,
                                 const core::CancelToken& token) {
        READ_USN_JOURNAL_DATA_V0 in{};
        in.StartUsn = range.startUsn;
        in.ReasonMask = 0xFFFF'FFFFU;
        in.UsnJournalID = range.journalId;
        std::vector<std::byte> buffer(std::size_t{64} * 1024);
        while (in.StartUsn < range.endUsn) {
            token.throwIfCancelled();
            const auto batch = readBatch(volume, in, buffer);
            if (const auto* status = std::get_if<UsnReadStatus>(&batch)) {
                return *status;
            }
            const DWORD bytes = std::get<DWORD>(batch);
            USN next = 0;
            std::memcpy(&next, buffer.data(), sizeof(USN));
            if (bytes <= sizeof(USN) || next <= in.StartUsn) {
                break; // No more records.
            }
            const auto changes = parseChanges(std::span(buffer).first(bytes), range.endUsn);
            if (!changes.empty()) {
                onBatch(changes);
            }
            in.StartUsn = next;
        }
        return UsnReadStatus::Complete;
    }

} // namespace icd::platform
