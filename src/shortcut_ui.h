#pragma once
#include "trigger.h"
#include "startup.h"
#include "theme.h"
#include <windows.h>
#include <functional>

namespace drawscr {
struct SettingsActions {
    std::function<BindError(const Trigger&)> bind;
    std::function<void(bool)> closed;
    std::function<bool(ThemeId)> set_theme;
    std::function<bool()> open_site;
    std::function<bool(bool)> set_start_minimized;
    std::function<std::wstring()> config_path;
    std::function<DWORD()> storage_error;
    std::function<DWORD()> trigger_error;
    std::wstring initial_notice;
};
HWND show_settings(HWND owner, const Trigger& current, StartupStore& startup,
                   SettingsActions actions, BindError initial_error = BindError::None,
                   bool start_minimized = false);
}
