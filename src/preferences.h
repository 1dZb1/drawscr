#pragma once
#include "trigger.h"
#include <string>

namespace drawscr {
enum class ConfigStatus { Missing, Loaded, Invalid, Unreadable };
struct TriggerConfig {
    Trigger trigger{};
    ConfigStatus status = ConfigStatus::Missing;
    DWORD error = 0;
};
std::wstring settings_path_for_executable(const std::wstring& executable);
bool executable_is_in_temp(const std::wstring& executable, const std::wstring& temp_dir);
TriggerConfig load_trigger_config(const std::wstring& path);
bool save_trigger_config(const std::wstring& path, const Trigger& trigger, DWORD& error);
bool load_start_minimized(const std::wstring& path);
bool save_start_minimized(const std::wstring& path, bool enabled, DWORD& error);

}
