#include "preferences.h"
#include <cwchar>
#include <cerrno>
#include <limits>

namespace drawscr {
std::wstring settings_path_for_executable(const std::wstring& executable) {
    auto slash = executable.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"Drawscr.ini"
                                       : executable.substr(0, slash + 1) + L"Drawscr.ini";
}
bool executable_is_in_temp(const std::wstring& executable, const std::wstring& temp_dir) {
    if (temp_dir.empty() || executable.size() < temp_dir.size())
        return false;
    std::wstring root = temp_dir;
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    if (executable.size() <= root.size())
        return false;
    return CompareStringOrdinal(executable.c_str(), static_cast<int>(root.size()), root.c_str(),
                                static_cast<int>(root.size()), TRUE) == CSTR_EQUAL &&
           (executable[root.size()] == L'\\' || executable[root.size()] == L'/');
}
namespace {
enum class ReadValue { Missing, Good, Invalid };
ReadValue read_number(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                      UINT& value) {
    wchar_t buf[64]{};
    DWORD count = GetPrivateProfileStringW(section, key, L"", buf, 64, path.c_str());
    if (!count)
        return ReadValue::Missing;
    wchar_t* end = nullptr;
    errno = 0;
    unsigned long v = wcstoul(buf, &end, 10);
    if (errno || end == buf || *end || buf[0] == L'-' || v > std::numeric_limits<UINT>::max())
        return ReadValue::Invalid;
    value = static_cast<UINT>(v);
    return ReadValue::Good;
}
}
TriggerConfig load_trigger_config(const std::wstring& path) {
    TriggerConfig result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
            result.status = ConfigStatus::Unreadable;
            result.error = e;
        }
        return result;
    }
    CloseHandle(file);
    UINT kind = 0, code = VK_SNAPSHOT, mods = 0, override_value = 0;
    auto k = read_number(path, L"Trigger", L"Kind", kind);
    if (k == ReadValue::Missing) {
        auto legacy = read_number(path, L"Shortcut", L"Key", code);
        if (legacy == ReadValue::Missing)
            return result;
        if (legacy == ReadValue::Invalid ||
            read_number(path, L"Shortcut", L"Modifiers", mods) == ReadValue::Invalid) {
            result.status = ConfigStatus::Invalid;
            return result;
        }
    } else {
        if (k == ReadValue::Invalid || kind > 1 ||
            read_number(path, L"Trigger", L"Code", code) == ReadValue::Invalid ||
            read_number(path, L"Trigger", L"Modifiers", mods) == ReadValue::Invalid ||
            read_number(path, L"Trigger", L"OverridePrintScreen", override_value) ==
                ReadValue::Invalid ||
            override_value > 1) {
            result.status = ConfigStatus::Invalid;
            return result;
        }
    }
    Trigger t{kind == 1 ? TriggerKind::Mouse : TriggerKind::Keyboard, mods, code,
              override_value != 0};
    if (!valid_trigger(t)) {
        result.status = ConfigStatus::Invalid;
        return result;
    }
    result.trigger = t;
    result.status = ConfigStatus::Loaded;
    return result;
}
bool save_trigger_config(const std::wstring& path, const Trigger& trigger, DWORD& error) {
    error = 0;
    if (!valid_trigger(trigger)) {
        error = ERROR_INVALID_PARAMETER;
        return false;
    }
    std::wstring values = L"Kind=" + std::to_wstring(trigger.kind == TriggerKind::Mouse ? 1 : 0);
    values.push_back(L'\0');
    values += L"Modifiers=" + std::to_wstring(trigger.modifiers);
    values.push_back(L'\0');
    values += L"Code=" + std::to_wstring(trigger.code);
    values.push_back(L'\0');
    values += L"OverridePrintScreen=" + std::to_wstring(trigger.override_printscreen ? 1 : 0);
    values.push_back(L'\0');
    SetLastError(0);
    if (!WritePrivateProfileSectionW(L"Trigger", values.c_str(), path.c_str())) {
        error = GetLastError();
        if (!error)
            error = ERROR_WRITE_FAULT;
        return false;
    }
    auto check = load_trigger_config(path);
    if (check.status != ConfigStatus::Loaded || !same_trigger(check.trigger, trigger)) {
        error = check.error ? check.error : ERROR_WRITE_FAULT;
        return false;
    }
    return true;
}
bool load_start_minimized(const std::wstring& path) {
    return GetPrivateProfileIntW(L"General", L"StartMinimized", 0, path.c_str()) == 1;
}
bool save_start_minimized(const std::wstring& path, bool enabled, DWORD& error) {
    error = 0;
    SetLastError(0);
    if (!WritePrivateProfileStringW(L"General", L"StartMinimized", enabled ? L"1" : L"0",
                                    path.c_str())) {
        error = GetLastError();
        if (!error)
            error = ERROR_WRITE_FAULT;
        return false;
    }
    UINT value = GetPrivateProfileIntW(L"General", L"StartMinimized", 2, path.c_str());
    if (value != (enabled ? 1u : 0u)) {
        error = ERROR_WRITE_FAULT;
        return false;
    }
    return true;
}

}
