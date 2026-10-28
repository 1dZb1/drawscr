#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

namespace drawscr {
enum class ThemeId { Mint = 0, Sky = 1, Amber = 2, Violet = 3 };
struct ThemeTokens {
    const wchar_t* name;
    const wchar_t* key;
    COLORREF accent, pressed, soft, border, focus, frame, ink;
};
const ThemeTokens& theme_for(ThemeId id);
const ThemeTokens& current_theme();
ThemeId current_theme_id();
void set_current_theme(ThemeId id);
ThemeId theme_from_name(const wchar_t* value);
ThemeId load_theme(const std::wstring& ini);
bool save_theme(const std::wstring& ini, ThemeId id);
inline Gdiplus::Color ui_color(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}
} // namespace drawscr
