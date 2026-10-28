#pragma once
#include <windows.h>
namespace drawscr {
inline constexpr wchar_t website_url[] = L"https://dzrobo.com";
using WebsiteOpener = INT_PTR (*)(HWND owner, const wchar_t* url, void* context);
inline bool open_website(HWND owner, WebsiteOpener opener, void* context = nullptr) {
    return opener && opener(owner, website_url, context) > 32;
}
} // namespace drawscr
