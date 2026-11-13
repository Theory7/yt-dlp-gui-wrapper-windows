// Windows 11 (Fluent) shell integration: everything here is a direct call into
// the OS. No look is re-implemented, the OS supplies it.
#pragma once
#include "app.h"

void FluentEarlyInit();                     // call before creating any window
bool FluentDarkModeWindow(HWND h);          // immersive dark title bar + dark controls
bool FluentMica(HWND h);                    // DWMWA_SYSTEMBACKDROP_TYPE = Mica
bool FluentAcrylic(HWND h);                 // acrylic-ish translucent surface
void FluentRoundedCorners(HWND h);
void FluentBorder(HWND h, COLORREF c);
// Paints the title bar itself instead of leaving it to the system's black
// non-client surface, so the frame reads as part of the app.
void FluentCaptionColors(HWND h, COLORREF caption, COLORREF text);
void FluentStyleControl(HWND h, const wchar_t* theme);  // dark theme for a control class
void FluentDpiAwareness();
UINT FluentCurrentDpi(HWND h);
