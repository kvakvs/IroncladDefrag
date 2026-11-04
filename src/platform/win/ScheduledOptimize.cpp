#include "platform/win/ScheduledOptimize.h"

#include "platform/win/Com.h"

#include <windows.h>

#include <taskschd.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <format>

namespace icd::platform {

    namespace {

        using Microsoft::WRL::ComPtr;

        // Where Optimize Drives keeps the state of its dialog.
        constexpr const wchar_t* settingsKey = L"SOFTWARE\\Microsoft\\Dfrg\\TaskSettings";

        // The words of an argument line.
        std::vector<std::wstring_view> wordsOf(std::wstring_view line) {
            std::vector<std::wstring_view> words;
            std::size_t at = 0;
            while (at < line.size()) {
                const std::size_t start = line.find_first_not_of(L" \t", at);
                if (start == std::wstring_view::npos) {
                    break;
                }
                const std::size_t end = std::min(line.find_first_of(L" \t", start), line.size());
                words.push_back(line.substr(start, end - start));
                at = end;
            }
            return words;
        }

        // Whether a word is the switch -x or /x (any case).
        bool isSwitch(std::wstring_view word, wchar_t letter) {
            return word.size() == 2 && (word[0] == L'-' || word[0] == L'/') && std::towlower(word[1]) == letter;
        }

        // What a word of the defrag arguments means for the drive choice.
        enum class WordKind : std::uint8_t {
            AllVolumes, // -c: every volume.
            Except, // -e: every volume but the listed ones.
            Flag, // Any other switch, kept as it is.
            Volume, // A listed volume.
        };

        // The kind of one word.
        WordKind kindOf(std::wstring_view word) {
            if (isSwitch(word, L'c')) {
                return WordKind::AllVolumes;
            }
            if (isSwitch(word, L'e')) {
                return WordKind::Except;
            }
            return word.front() == L'-' || word.front() == L'/' ? WordKind::Flag : WordKind::Volume;
        }

        // A volume name in a comparable form: lower case, without a trailing backslash ("D:\" and "d:" match).
        std::wstring comparable(std::wstring_view name) {
            std::wstring text(name);
            std::ranges::transform(text, text.begin(),
                                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            while (!text.empty() && text.back() == L'\\') {
                text.pop_back();
            }
            return text;
        }

        // Whether a listed volume is the volume known by `names`.
        bool sameVolume(std::wstring_view listed, std::span<const std::wstring> names) {
            const std::wstring key = comparable(listed);
            return std::ranges::any_of(names,
                                       [&](const std::wstring& n) { return !n.empty() && comparable(n) == key; });
        }

        // Words joined by single spaces, empty words left out.
        std::wstring joined(std::span<const std::wstring> words) {
            std::wstring line;
            for (const std::wstring& word : words) {
                if (!word.empty()) {
                    line += line.empty() ? word : L" " + word;
                }
            }
            return line;
        }

        // The TaskSettings values of a selection, as the dialog writes them.
        struct SettingsValues {
            DWORD allVolumes = 1; // fAllVolumes.
            DWORD exclude = 0; // fExclude.
            std::wstring volumes = L" "; // Volumes: the list, or one space for none.
        };

        // The TaskSettings values for a selection.
        SettingsValues settingsOf(const ScheduleSelection& selection) {
            if (selection.mode == ScheduleMode::All) {
                return {};
            }
            const std::wstring list = joined(selection.volumes);
            return {.allVolumes = 0,
                    .exclude = selection.mode == ScheduleMode::AllExcept ? 1U : 0U,
                    .volumes = list.empty() ? L" " : list};
        }

        // Owns an open registry key.
        class RegKey {
        public:
            RegKey() = default;
            RegKey(const RegKey&) = delete;
            RegKey& operator=(const RegKey&) = delete;
            ~RegKey() {
                if (key_ != nullptr) {
                    RegCloseKey(key_);
                }
            }

            // Where RegCreateKeyExW stores the key this object then owns.
            [[nodiscard]] HKEY* out() { return &key_; }

            // The key for API calls (still owned).
            [[nodiscard]] HKEY get() const { return key_; }

        private:
            HKEY key_ = nullptr; // Open key, or null.
        };

        // Writes the dialog's values; an error text, empty on success.
        std::wstring writeSettings(const SettingsValues& values) {
            RegKey key;
            LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, settingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                             key.out(), nullptr);
            if (status == ERROR_SUCCESS) {
                status = RegSetValueExW(key.get(), L"fAllVolumes", 0, REG_DWORD,
                                        reinterpret_cast<const BYTE*>(&values.allVolumes), sizeof(DWORD));
            }
            if (status == ERROR_SUCCESS) {
                status = RegSetValueExW(key.get(), L"fExclude", 0, REG_DWORD,
                                        reinterpret_cast<const BYTE*>(&values.exclude), sizeof(DWORD));
            }
            if (status == ERROR_SUCCESS) {
                const auto bytes = static_cast<DWORD>((values.volumes.size() + 1) * sizeof(wchar_t));
                status = RegSetValueExW(key.get(), L"Volumes", 0, REG_SZ,
                                        reinterpret_cast<const BYTE*>(values.volumes.c_str()), bytes);
            }
            return status == ERROR_SUCCESS ? std::wstring()
                                           : std::format(L"TaskSettings not written (error {})", status);
        }

        // The defrag task, opened for reading and updating.
        struct DefragTask {
            ComPtr<ITaskFolder> folder; // \Microsoft\Windows\Defrag.
            ComPtr<IRegisteredTask> task; // ScheduledDefrag.
            ComPtr<ITaskDefinition> definition; // Its definition, to update.
            ComPtr<IExecAction> exec; // Its first action, the defrag.exe call.
        };

        // The folder of the defrag task; null when Task Scheduler cannot be asked.
        ComPtr<ITaskFolder> defragFolder() {
            ComPtr<ITaskService> service;
            if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)))) {
                return nullptr;
            }
            const Variant none;
            if (FAILED(service->Connect(none.value, none.value, none.value, none.value))) {
                return nullptr;
            }
            ComPtr<ITaskFolder> folder;
            const Bstr path(L"\\Microsoft\\Windows\\Defrag");
            return SUCCEEDED(service->GetFolder(path.get(), &folder)) ? folder : nullptr;
        }

        // The first action of a definition as an exec action; null when it is something else.
        ComPtr<IExecAction> execOf(ITaskDefinition& definition) {
            ComPtr<IActionCollection> actions;
            ComPtr<IAction> action;
            ComPtr<IExecAction> exec;
            if (FAILED(definition.get_Actions(&actions)) || FAILED(actions->get_Item(1, &action)) ||
                FAILED(action.As(&exec))) {
                return nullptr;
            }
            return exec;
        }

        // The defrag task and its action; nullopt when any part is missing.
        std::optional<DefragTask> openTask() {
            DefragTask t;
            t.folder = defragFolder();
            const Bstr name(L"ScheduledDefrag");
            if (!t.folder || FAILED(t.folder->GetTask(name.get(), &t.task)) ||
                FAILED(t.task->get_Definition(&t.definition))) {
                return std::nullopt;
            }
            t.exec = execOf(*t.definition.Get());
            return t.exec ? std::optional(std::move(t)) : std::nullopt;
        }

        // Registers the changed definition in place, under the principal it already has.
        HRESULT updateTask(DefragTask& t) {
            ComPtr<IPrincipal> principal;
            TASK_LOGON_TYPE logon = TASK_LOGON_SERVICE_ACCOUNT;
            Bstr user;
            if (SUCCEEDED(t.definition->get_Principal(&principal))) {
                principal->get_LogonType(&logon);
                principal->get_UserId(user.out());
            }
            Variant userId;
            if (user.get() != nullptr) {
                userId.value.vt = VT_BSTR;
                userId.value.bstrVal = SysAllocString(user.get());
            }
            const Variant none;
            const Bstr name(L"ScheduledDefrag");
            ComPtr<IRegisteredTask> registered;
            return t.folder->RegisterTaskDefinition(name.get(), t.definition.Get(), TASK_UPDATE, userId.value,
                                                    none.value, logon, none.value, &registered);
        }

    } // namespace

    ScheduleSelection parseScheduleArguments(std::wstring_view arguments) {
        ScheduleSelection selection;
        std::vector<std::wstring> flags;
        bool all = false;
        bool except = false;
        for (const std::wstring_view word : wordsOf(arguments)) {
            switch (kindOf(word)) {
            case WordKind::AllVolumes:
                all = true;
                break;
            case WordKind::Except:
                except = true;
                break;
            case WordKind::Flag:
                flags.emplace_back(word);
                break;
            case WordKind::Volume:
                selection.volumes.emplace_back(word);
                break;
            }
        }
        selection.flags = joined(flags);
        selection.mode = all ? ScheduleMode::All : (except ? ScheduleMode::AllExcept : ScheduleMode::Only);
        if (all) {
            selection.volumes.clear(); // defrag ignores a list next to -c.
        }
        return selection;
    }

    std::wstring scheduleArguments(const ScheduleSelection& selection) {
        if (selection.mode == ScheduleMode::All) {
            return joined(std::vector<std::wstring>{L"-c", selection.flags});
        }
        const std::wstring except = selection.mode == ScheduleMode::AllExcept ? L"-e" : L"";
        return joined(std::vector<std::wstring>{joined(selection.volumes), except, selection.flags});
    }

    bool scheduleCovers(const ScheduleSelection& selection, std::span<const std::wstring> names) {
        if (selection.mode == ScheduleMode::All) {
            return true;
        }
        const bool listed =
            std::ranges::any_of(selection.volumes, [&](const std::wstring& v) { return sameVolume(v, names); });
        return selection.mode == ScheduleMode::AllExcept ? !listed : listed;
    }

    std::optional<ScheduleSelection> withScheduled(ScheduleSelection selection, std::span<const std::wstring> names,
                                                   bool scheduled) {
        if (names.empty() || scheduleCovers(selection, names) == scheduled) {
            return selection; // Already as wanted.
        }
        if (selection.mode == ScheduleMode::All) {
            selection.mode = ScheduleMode::AllExcept; // Only reached when taking a volume out.
        }
        // Listing the volume takes it out of AllExcept and puts it into Only; dropping it does the reverse.
        const bool list = (selection.mode == ScheduleMode::Only) == scheduled;
        if (list) {
            selection.volumes.push_back(names.front());
            return selection;
        }
        std::erase_if(selection.volumes, [&](const std::wstring& v) { return sameVolume(v, names); });
        if (selection.volumes.empty()) {
            if (selection.mode == ScheduleMode::Only) {
                return std::nullopt; // The last scheduled drive.
            }
            selection.mode = ScheduleMode::All;
        }
        return selection;
    }

    std::optional<ScheduleState> readSchedule() {
        const ComScope com;
        const std::optional<DefragTask> t = openTask();
        if (!t) {
            return std::nullopt;
        }
        VARIANT_BOOL enabled = VARIANT_FALSE;
        Bstr arguments;
        if (FAILED(t->task->get_Enabled(&enabled)) || FAILED(t->exec->get_Arguments(arguments.out()))) {
            return std::nullopt;
        }
        return ScheduleState{.taskEnabled = enabled != VARIANT_FALSE,
                             .selection = parseScheduleArguments(arguments.text())};
    }

    std::wstring writeSchedule(const ScheduleSelection& selection) {
        const ComScope com;
        std::optional<DefragTask> t = openTask();
        if (!t) {
            return L"the ScheduledDefrag task cannot be opened";
        }
        const Bstr arguments(scheduleArguments(selection).c_str());
        if (FAILED(t->exec->put_Arguments(arguments.get()))) {
            return L"the task's arguments cannot be set";
        }
        if (const HRESULT hr = updateTask(*t); FAILED(hr)) {
            return std::format(L"the task was not updated (0x{:08X})", static_cast<unsigned long>(hr));
        }
        return writeSettings(settingsOf(selection));
    }

} // namespace icd::platform
