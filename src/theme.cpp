#include "theme.h"
#include <atomic>
namespace drawscr {
static const ThemeTokens themes[] = {
    {L"Mint", L"mint", RGB(92, 207, 183), RGB(75, 180, 158), RGB(48, 90, 82), RGB(105, 221, 195),
     RGB(140, 226, 207), RGB(111, 224, 197), RGB(18, 35, 38)},
    {L"Sky", L"sky", RGB(116, 182, 220), RGB(92, 158, 198), RGB(45, 76, 93), RGB(145, 204, 237),
     RGB(164, 215, 241), RGB(136, 201, 233), RGB(18, 35, 38)},
    {L"Amber", L"amber", RGB(220, 173, 106), RGB(195, 148, 84), RGB(92, 69, 42), RGB(238, 197, 138),
     RGB(242, 207, 160), RGB(233, 190, 128), RGB(18, 35, 38)},
    {L"Violet", L"violet", RGB(178, 151, 218), RGB(151, 124, 193), RGB(70, 58, 92),
     RGB(205, 181, 237), RGB(218, 198, 242), RGB(195, 171, 231), RGB(18, 35, 38)}};
static std::atomic<int> active{0};
const ThemeTokens& theme_for(ThemeId id) {
    int i = static_cast<int>(id);
    return themes[i >= 0 && i < 4 ? i : 0];
}
ThemeId current_theme_id() {
    int i = active.load();
    return static_cast<ThemeId>(i >= 0 && i < 4 ? i : 0);
}
const ThemeTokens& current_theme() {
    return theme_for(current_theme_id());
}
void set_current_theme(ThemeId id) {
    int i = static_cast<int>(id);
    active.store(i >= 0 && i < 4 ? i : 0);
}
ThemeId theme_from_name(const wchar_t* value) {
    if (value)
        for (int i = 0; i < 4; ++i)
            if (_wcsicmp(value, themes[i].key) == 0)
                return static_cast<ThemeId>(i);
    return ThemeId::Mint;
}
ThemeId load_theme(const std::wstring& ini) {
    wchar_t name[32]{};
    GetPrivateProfileStringW(L"Appearance", L"Theme", L"mint", name, 32, ini.c_str());
    return theme_from_name(name);
}
bool save_theme(const std::wstring& ini, ThemeId id) {
    int i = static_cast<int>(id);
    return i >= 0 && i < 4 &&
           WritePrivateProfileStringW(L"Appearance", L"Theme", themes[i].key, ini.c_str()) != FALSE;
}
} // namespace drawscr
