#include "../../precompiled.h"
#include "VolumeMoveOperations.h"

#include "DriveEnumerator.h"
#include "UniqueHandle.h"

#include <shellapi.h>
#include <winioctl.h>

#include <limits>
#include <sstream>

namespace icd::win {

namespace {
std::wstring FormatWindowsError(DWORD error)
{
    if (error == ERROR_SUCCESS) {
        return L"success";
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        error,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"unknown Windows error";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) {
        message.pop_back();
    }
    return message;
}

// Opens the volume for FSCTL_MOVE_FILE, which requires write-capable volume access.
UniqueHandle OpenVolumeForMove(const std::wstring& rootPath)
{
    const std::wstring volumePath = ToVolumePath(rootPath);
    if (volumePath.empty()) {
        return UniqueHandle();
    }

    return UniqueHandle(CreateFileW(volumePath.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr));
}

UniqueHandle OpenFileForMove(const std::filesystem::path& path)
{
    return UniqueHandle(CreateFileW(path.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    nullptr));
}

bool IsProcessElevated()
{
    UniqueHandle token;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    token.Reset(rawToken);

    TOKEN_ELEVATION elevation{};
    DWORD bytesReturned = 0;
    if (!GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof(elevation), &bytesReturned)) {
        return false;
    }
    return elevation.TokenIsElevated != 0;
}

bool EnableManageVolumePrivilege()
{
    UniqueHandle token;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &rawToken)) {
        return false;
    }
    token.Reset(rawToken);

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_MANAGE_VOLUME_NAME, &privileges.Privileges[0].Luid)) {
        return false;
    }

    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(token.Get(), FALSE, &privileges, sizeof(privileges), nullptr, nullptr)) {
        return false;
    }
    return GetLastError() == ERROR_SUCCESS;
}

std::wstring BuildPrivilegeMessage(const MoveExecutionPrivilegeStatus& status)
{
    if (status.canMoveFiles) {
        return L"Move execution privileges are available.";
    }

    std::wstringstream message;
    message << L"Move execution requires an elevated administrator process with the manage-volume privilege";
    if (status.lastError != 0) {
        message << L" (" << FormatWindowsError(status.lastError) << L")";
    }
    message << L".";
    return message.str();
}
} // namespace

// Checks whether this process can open the volume and enable the privilege needed for file movement.
MoveExecutionPrivilegeStatus ProbeMovePrivileges(const std::wstring& rootPath)
{
    MoveExecutionPrivilegeStatus status;
    status.isElevated = IsProcessElevated();
    status.manageVolumePrivilegeEnabled = EnableManageVolumePrivilege();

    UniqueHandle volume = OpenVolumeForMove(rootPath);
    status.canOpenVolumeForMove = volume.IsValid();
    status.lastError = status.canOpenVolumeForMove ? ERROR_SUCCESS : GetLastError();
    status.canMoveFiles = status.isElevated && status.manageVolumePrivilegeEnabled && status.canOpenVolumeForMove;
    status.message = BuildPrivilegeMessage(status);
    return status;
}

// Starts a new elevated copy of the current executable so Windows can show the UAC prompt.
bool RelaunchElevated()
{
    std::vector<wchar_t> executable(MAX_PATH);
    DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    while (length == executable.size()) {
        executable.resize(executable.size() * 2);
        length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    }

    if (length == 0) {
        return false;
    }

    executable[length] = L'\0';
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"runas", executable.data(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

// Performs one FSCTL_MOVE_FILE call against the target volume and file.
FileMoveResult MoveFileClusters(const FileMoveRequest& request)
{
    FileMoveResult result;
    if (request.clusterCount.getValue() == 0 ||
        request.clusterCount.getValue() > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())) {
        result.errorCode = ERROR_INVALID_PARAMETER;
        result.message = L"cluster count is unsupported by FSCTL_MOVE_FILE";
        return result;
    }

    UniqueHandle volume = OpenVolumeForMove(request.rootPath);
    if (!volume.IsValid()) {
        result.errorCode = GetLastError();
        result.message = L"unable to open volume for move: " + FormatWindowsError(result.errorCode);
        return result;
    }

    UniqueHandle file = OpenFileForMove(request.filePath);
    if (!file.IsValid()) {
        result.errorCode = GetLastError();
        result.message = L"unable to open file for move: " + FormatWindowsError(result.errorCode);
        return result;
    }

    MOVE_FILE_DATA moveData{};
    moveData.FileHandle = file.Get();
    moveData.StartingVcn.QuadPart = static_cast<LONGLONG>(request.startingVcn.getValue());
    moveData.StartingLcn.QuadPart = static_cast<LONGLONG>(request.targetLcn.getValue());
    moveData.ClusterCount = static_cast<DWORD>(request.clusterCount.getValue());

    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(volume.Get(),
                                    FSCTL_MOVE_FILE,
                                    &moveData,
                                    sizeof(moveData),
                                    nullptr,
                                    0,
                                    &bytesReturned,
                                    nullptr);
    result.succeeded = ok != FALSE;
    result.errorCode = result.succeeded ? ERROR_SUCCESS : GetLastError();
    result.message = result.succeeded ? L"move completed" : L"move failed: " + FormatWindowsError(result.errorCode);
    return result;
}

} // namespace icd::win
