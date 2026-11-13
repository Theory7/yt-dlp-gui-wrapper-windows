#include "fluent.h"
#include <dwmapi.h>
#include <uxtheme.h>

// Undocumented uxtheme entry points used by Windows 11 itself for dark mode.
typedef int (WINAPI* PFN_SetPreferredAppMode)(int);
typedef BOOL (WINAPI* PFN_AllowDarkModeForWindow)(HWND, BOOL);
typedef void (WINAPI* PFN_FlushMenuThemes)();

static PFN_SetPreferredAppMode pSetPreferredAppMode = nullptr;
static PFN_AllowDarkModeForWindow pAllowDarkModeForWindow = nullptr;
static PFN_FlushMenuThemes pFlushMenuThemes = nullptr;
static bool g_darkInited = false;

// DWM attributes (values from the Windows 11 SDK, spelled out so older headers work)
static const DWORD kAttrDarkTitle = 20;     // DWMWA_USE_IMMERSIVE_DARK_MODE
static const DWORD kAttrCorner = 33;        // DWMWA_WINDOW_CORNER_PREFERENCE
static const DWORD kAttrBorder = 34;        // DWMWA_BORDER_COLOR
static const DWORD kAttrCaption = 35;       // DWMWA_CAPTION_COLOR
static const DWORD kAttrText = 36;          // DWMWA_TEXT_COLOR
static const DWORD kAttrBackdrop = 38;      // DWMWA_SYSTEMBACKDROP_TYPE

static void LoadDarkApis() {
    if (g_darkInited) return;
    g_darkInited = true;
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    pSetPreferredAppMode = (PFN_SetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    pAllowDarkModeForWindow = (PFN_AllowDarkModeForWindow)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    pFlushMenuThemes = (PFN_FlushMenuThemes)GetProcAddress(ux, MAKEINTRESOURCEA(136));
}

void FluentEarlyInit() {
    LoadDarkApis();
    // PreferredAppMode::ForceDark - the app is a dark surface on purpose; do not follow light mode.
    if (pSetPreferredAppMode) pSetPreferredAppMode(2);
    if (pFlushMenuThemes) pFlushMenuThemes();
}

void FluentDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

UINT FluentCurrentDpi(HWND h) {
    UINT dpi = h ? GetDpiForWindow(h) : 0;
    if (!dpi) {
        HDC dc = GetDC(nullptr);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    return dpi ? dpi : 96;
}

bool FluentDarkModeWindow(HWND h) {
    LoadDarkApis();
    BOOL dark = TRUE;
    HRESULT hr = DwmSetWindowAttribute(h, kAttrDarkTitle, &dark, sizeof(dark));
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(h, TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    return SUCCEEDED(hr);
}

bool FluentMica(HWND h) {
    // DWMSBT_MAINWINDOW == 2 (Mica). Win11 22H2+; older builds fail and we paint instead.
    int backdrop = 2;
    HRESULT hr = DwmSetWindowAttribute(h, kAttrBackdrop, &backdrop, sizeof(backdrop));
    if (FAILED(hr)) return false;
    // Do NOT extend the frame into the client. With the frame extended the
    // whole client becomes a DWM glass surface: the app's own fills turn into
    // the blurred wallpaper while child controls stay opaque, which showed up
    // as "black boxes" around every label, and the caption fell back to a
    // solid black strip when the window was maximized. Keeping the frame on
    // the non-client area keeps the Mica title bar without either problem.
    return true;
}

bool FluentAcrylic(HWND h) {
    // Attempt acrylic (translucent frosted-glass) via DWM extended frame + dark surface.
    // Falls back gracefully if DWM composition is unavailable.
    MARGINS m = { -1, -1, -1, -1 };
    if (FAILED(DwmExtendFrameIntoClientArea(h, &m))) return false;
    return true;
}

void FluentRoundedCorners(HWND h) {
    int pref = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(h, kAttrCorner, &pref, sizeof(pref));
}

void FluentBorder(HWND h, COLORREF c) {
    COLORREF v = c;
    DwmSetWindowAttribute(h, kAttrBorder, &v, sizeof(v));
}

void FluentCaptionColors(HWND h, COLORREF caption, COLORREF text) {
    COLORREF c = caption;
    COLORREF t = text;
    DwmSetWindowAttribute(h, kAttrCaption, &c, sizeof(c));
    DwmSetWindowAttribute(h, kAttrText, &t, sizeof(t));
}

void FluentStyleControl(HWND h, const wchar_t* theme) {
    if (!h) return;
    SetWindowTheme(h, theme, nullptr);
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(h, TRUE);
}
