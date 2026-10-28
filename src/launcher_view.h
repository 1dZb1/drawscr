#pragma once
#include <windows.h>
#include <string>
namespace drawscr {
int launcher_hit(POINT client, UINT dpi);
void paint_launcher(HDC target, UINT dpi, int hover, int focus, bool focused,
                    const std::wstring& shortcut);
}
