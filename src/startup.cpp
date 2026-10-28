#include "startup.h"
#include <cwchar>
#include <utility>
#include <vector>

namespace drawscr {
namespace {
constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t value_name[] = L"Drawscr";

LSTATUS read_value(void*, std::wstring& value) {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS)
        return status;
    DWORD type = 0, size = 0;
    status = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) {
        RegCloseKey(key);
        return status;
    }
    if (type != REG_SZ || size < sizeof(wchar_t) || size > 65536 || size % sizeof(wchar_t)) {
        RegCloseKey(key);
        return ERROR_INVALID_DATA;
    }
    std::vector<wchar_t> data(size / sizeof(wchar_t) + 1, 0);
    status = RegQueryValueExW(key, value_name, nullptr, &type, reinterpret_cast<BYTE*>(data.data()),
                              &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return status;
    if (type != REG_SZ || size % sizeof(wchar_t))
        return ERROR_INVALID_DATA;
    value.assign(data.data());
    return ERROR_SUCCESS;
}
LSTATUS write_value(void*, const std::wstring& value) {
    HKEY key = nullptr;
    LSTATUS status =
        RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
        return status;
    status =
        RegSetValueExW(key, value_name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status;
}
LSTATUS remove_value(void*) {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS)
        return status;
    status = RegDeleteValueW(key, value_name);
    RegCloseKey(key);
    return status;
}
StartupValueOps native_ops() {
    return {read_value, write_value, remove_value, nullptr};
}
}
RegistryStartupStore::RegistryStartupStore(std::wstring path, const StartupValueOps* ops)
    : command_(L"\"" + std::move(path) + L"\" --startup"), ops_(ops ? *ops : native_ops()) {}

StartupState RegistryStartupStore::query() {
    std::wstring value;
    last_error_ = ops_.read ? ops_.read(ops_.context, value) : ERROR_INVALID_FUNCTION;
    if (last_error_ == ERROR_FILE_NOT_FOUND)
        return StartupState::Off;
    if (last_error_ == ERROR_INVALID_DATA)
        return StartupState::Other;
    if (last_error_ != ERROR_SUCCESS)
        return StartupState::Error;
    return _wcsicmp(value.c_str(), command_.c_str()) == 0 ? StartupState::On : StartupState::Other;
}
bool RegistryStartupStore::set(bool enabled, std::wstring& error) {
    error.clear();
    StartupState before = query();
    if (before == StartupState::Error) {
        error = L"Windows could not read Drawscr startup (error " + std::to_wstring(last_error_) +
                L").";
        return false;
    }
    if ((enabled && before == StartupState::On) || (!enabled && before == StartupState::Off))
        return true;
    LSTATUS status =
        enabled ? (ops_.write ? ops_.write(ops_.context, command_) : ERROR_INVALID_FUNCTION)
                : (ops_.remove ? ops_.remove(ops_.context) : ERROR_INVALID_FUNCTION);
    if (status != ERROR_SUCCESS && !(!enabled && status == ERROR_FILE_NOT_FOUND)) {
        error =
            L"Windows could not change Drawscr startup (error " + std::to_wstring(status) + L").";
        return false;
    }
    StartupState after = query();
    if (after != (enabled ? StartupState::On : StartupState::Off)) {
        error = after == StartupState::Error
                    ? L"Windows could not confirm Drawscr startup (error " +
                          std::to_wstring(last_error_) + L")."
                    : L"Windows did not keep the requested startup setting. Please try again.";
        return false;
    }
    return true;
}
bool toggle_startup(StartupStore& store, StartupState& state, std::wstring& error) {
    error.clear();
    if (state == StartupState::Error) {
        state = store.query();
        if (state == StartupState::Error) {
            LSTATUS code = store.error_code();
            error = code ? L"Windows could not read Drawscr startup (error " +
                               std::to_wstring(code) + L")."
                         : L"Windows startup settings could not be read.";
            return false;
        }
    }
    const bool enable = state != StartupState::On;
    const bool changed = store.set(enable, error);
    state = store.query();
    if (!changed) {
        if (error.empty())
            error = L"Windows could not change Drawscr startup.";
        return false;
    }
    if (state != (enable ? StartupState::On : StartupState::Off)) {
        error = L"Windows could not confirm the requested startup setting.";
        return false;
    }
    error.clear();
    return true;
}
}
