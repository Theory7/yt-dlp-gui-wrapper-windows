// string / path / ini helpers
#include "app.h"
#include <cstdarg>
#include <cstdio>
#include <shlwapi.h>

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(CP_UTF8, flags, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return std::wstring();
    }
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string o((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) b--;
    return s.substr(a, b - a);
}

// Turns any text (usually a playlist title) into a folder name that Windows
// accepts: invalid characters and control codes are dropped, trailing dots and
// spaces are removed, and the result is kept short enough for a full path.
std::wstring SafeFolderName(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (c < 0x20 || c == 0x7f) continue;
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') continue;
        o.push_back(c);
    }
    o = Trim(o);
    while (!o.empty() && (o.back() == L'.' || o.back() == L' ')) o.pop_back();
    if (o.size() > 80) {
        o.resize(80);
        // Do not leave half of a surrogate pair at the cut.
        if (!o.empty() && o.back() >= 0xD800 && o.back() <= 0xDBFF) o.pop_back();
        while (!o.empty() && (o.back() == L'.' || o.back() == L' ')) o.pop_back();
    }
    if (o.empty()) o = L"Playlist";
    // CON, PRN, NUL, COM1, ... are reserved device names on Windows.
    static const wchar_t* kReserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
        L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
    };
    for (const wchar_t* r : kReserved) {
        if (_wcsicmp(o.c_str(), r) == 0) { o += L"_"; break; }
    }
    return o;
}

// MSVCRT style argument quoting
std::wstring QuoteArg(const std::wstring& s) {
    if (!s.empty() && s.find_first_of(L" \t\n\v\"") == std::wstring::npos) return s;
    std::wstring r;
    r.push_back(L'"');
    for (size_t i = 0; i < s.size(); i++) {
        size_t bs = 0;
        while (i < s.size() && s[i] == L'\\') { bs++; i++; }
        if (i == s.size()) { r.append(bs * 2, L'\\'); break; }
        if (s[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(s[i]); }
    }
    r.push_back(L'"');
    return r;
}

std::wstring BuildCmdLine(const std::wstring& exe, const std::vector<std::wstring>& args) {
    std::wstring c = QuoteArg(exe);
    for (const auto& a : args) {
        c.push_back(L' ');
        c += QuoteArg(a);
    }
    return c;
}

std::vector<std::wstring> SplitArgs(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool inQuote = false;
    for (wchar_t c : s) {
        if (c == L'"') { inQuote = !inQuote; continue; }
        if (!inQuote && (c == L' ' || c == L'\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (!n) return L".";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

std::wstring ParentDir(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    if (pos == 2 && p.size() > 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring r = a;
    if (r.back() != L'\\' && r.back() != L'/') r.push_back(L'\\');
    r += b;
    return r;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring NowStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[64];
    swprintf(b, 64, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return b;
}

// ---------- configuration (portable ini next to the exe)
static std::wstring KeyFor(const wchar_t* prefix, int id) {
    wchar_t b[32];
    swprintf(b, 32, L"%s%d", prefix, id);
    return b;
}

// Remembers the *restored* rectangle. Saving GetWindowRect() while maximized
// stored the maximized size as the restore size, so the window came back
// full-screen with its resize borders hanging off the screen and could not be
// resized like a normal app.
static bool SaveWindowPlacement() {
    if (g_cfgPath.empty() || !g_hMain || IsIconic(g_hMain)) return false;
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(g_hMain, &wp)) return false;
    RECT r = wp.rcNormalPosition;
    wchar_t v[128];
    swprintf(v, 128, L"%d,%d,%d,%d,%d", (int)r.left, (int)r.top, (int)(r.right - r.left),
             (int)(r.bottom - r.top), wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
    return WritePrivateProfileStringW(L"window", L"rect", v, g_cfgPath.c_str()) != FALSE;
}

bool IniSave() {
    if (g_cfgPath.empty()) return false;    for (const auto& w : g_widgets) {
        if (!w.h || w.id <= 0 || w.page > 0) continue;  // option pages are saved below
        if (w.id == IDC_LOG) continue;
        wchar_t cls[64] = { 0 };
        GetClassNameW(w.h, cls, 64);
        std::wstring clsW = cls;
        LONG_PTR style = GetWindowLongPtrW(w.h, GWL_STYLE);
        if (clsW == L"Button" &&
            (IsCheckCtl(w.h) || (style & BS_TYPEMASK) == BS_AUTOCHECKBOX)) {
            wchar_t v[2] = { SendMessageW(w.h, BM_GETCHECK, 0, 0) == BST_CHECKED ? L'1' : L'0', 0 };
            WritePrivateProfileStringW(L"controls", KeyFor(L"c", w.id).c_str(), v, g_cfgPath.c_str());
        } else if (clsW == L"ComboBox" || clsW == L"YtDlpCombo") {
            wchar_t v[16];
            swprintf(v, 16, L"%d", (int)SendMessageW(w.h, CB_GETCURSEL, 0, 0));
            WritePrivateProfileStringW(L"controls", KeyFor(L"c", w.id).c_str(), v, g_cfgPath.c_str());
        } else if (clsW == L"Edit") {
            int n = GetWindowTextLengthW(w.h);
            std::wstring txt((size_t)n + 1, L'\0');
            GetWindowTextW(w.h, &txt[0], n + 1);
            txt.resize((size_t)n);
            WritePrivateProfileStringW(L"controls", KeyFor(L"t", w.id).c_str(), txt.c_str(), g_cfgPath.c_str());
        }
    }
    // page controls
    for (int p = 1; p < g_optPageCount; p++) {
        for (int i = 0; i < g_optPages[p].count; i++) {
            int id = PageBase(p) + i;
            HWND h = Ctl(id);
            if (!h) continue;
            const OptDef& d = g_optPages[p].defs[i];
            if (d.kind == OPT_CHECK) {
                wchar_t v[2] = { SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED ? L'1' : L'0', 0 };
                WritePrivateProfileStringW(L"controls", KeyFor(L"c", id).c_str(), v, g_cfgPath.c_str());
            } else if (d.kind == OPT_COMBO) {
                wchar_t v[16];
                swprintf(v, 16, L"%d", (int)SendMessageW(h, CB_GETCURSEL, 0, 0));
                WritePrivateProfileStringW(L"controls", KeyFor(L"c", id).c_str(), v, g_cfgPath.c_str());
            } else if (d.kind == OPT_EDIT || d.kind == OPT_FILE) {
                int n = GetWindowTextLengthW(h);
                std::wstring txt((size_t)n + 1, L'\0');
                GetWindowTextW(h, &txt[0], n + 1);
                txt.resize((size_t)n);
                WritePrivateProfileStringW(L"controls", KeyFor(L"t", id).c_str(), txt.c_str(), g_cfgPath.c_str());
            }
        }
    }
    // window placement
    SaveWindowPlacement();
    return true;
}

// Closing the window only remembers where it was. Control values are written
// by the Apply button on the Settings page, so nothing is persisted silently.
bool IniSaveWindow() {
    return SaveWindowPlacement();
}

static bool LoadCtl(HWND h, int id, const OptDef* def) {
    wchar_t cls[64] = { 0 };
    GetClassNameW(h, cls, 64);
    std::wstring clsW = cls;
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    if (def) {
        if (def->kind == OPT_CHECK) {
            int d = (def->def && wcscmp(def->def, L"1") == 0) ? 1 : 0;
            int v = GetPrivateProfileIntW(L"controls", KeyFor(L"c", id).c_str(), d, g_cfgPath.c_str());
            SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
            return true;
        }
        if (def->kind == OPT_COMBO) {
            int d = 0;
            if (def->def && def->def[0]) {
                int cnt = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
                for (int i = 0; i < cnt; i++) {
                    wchar_t buf[512] = { 0 };
                    SendMessageW(h, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
                    if (wcscmp(buf, def->def) == 0) { d = i; break; }
                }
            }
            int v = GetPrivateProfileIntW(L"controls", KeyFor(L"c", id).c_str(), d, g_cfgPath.c_str());
            int cnt = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
            if (v < 0 || v >= cnt) v = 0;
            SendMessageW(h, CB_SETCURSEL, v, 0);
            return true;
        }
        if (def->kind == OPT_EDIT || def->kind == OPT_FILE) {
            wchar_t buf[4096] = { 0 };
            GetPrivateProfileStringW(L"controls", KeyFor(L"t", id).c_str(),
                                     def->def ? def->def : L"", buf, 4096, g_cfgPath.c_str());
            SetWindowTextW(h, buf);
            return true;
        }
        return false;
    }
    if (clsW == L"Button" &&
        (IsCheckCtl(h) || (style & BS_TYPEMASK) == BS_AUTOCHECKBOX)) {
        int v = GetPrivateProfileIntW(L"controls", KeyFor(L"c", id).c_str(), 0, g_cfgPath.c_str());
        SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(h, nullptr, TRUE);
        return true;
    }
    if (clsW == L"ComboBox" || clsW == L"YtDlpCombo") {
        int v = GetPrivateProfileIntW(L"controls", KeyFor(L"c", id).c_str(),
                                      DefaultComboSel(id), g_cfgPath.c_str());
        int cnt = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
        if (v < 0 || v >= cnt) v = 0;
        SendMessageW(h, CB_SETCURSEL, v, 0);
        return true;
    }
    if (clsW == L"Edit") {
        wchar_t buf[4096] = { 0 };
        GetPrivateProfileStringW(L"controls", KeyFor(L"t", id).c_str(), L"", buf, 4096, g_cfgPath.c_str());
        SetWindowTextW(h, buf);
        return true;
    }
    return false;
}

bool IniLoad() {
    if (g_cfgPath.empty() || !FileExists(g_cfgPath)) return false;
    // shell + Download page controls (the option page loop below handles pages 1..n)
    for (const auto& w : g_widgets) {
        if (!w.h || w.id <= 0 || w.page > 0) continue;
        if (w.id == IDC_LOG || w.id == IDC_STATUS ||
            w.id == IDC_TOOLS || w.id == IDC_PROGRESS)
            continue;
        LoadCtl(w.h, w.id, nullptr);
    }
    for (int p = 1; p < g_optPageCount; p++) {
        for (int i = 0; i < g_optPages[p].count; i++) {
            HWND h = Ctl(PageBase(p) + i);
            if (!h) continue;
            const OptDef& d = g_optPages[p].defs[i];
            LoadCtl(h, PageBase(p) + i, &d);
            // App-only edit fields keep their documented default when an older
            // settings file has no usable value for them (option ids can shift
            // when the page is trimmed).
            if (d.kind == OPT_EDIT && d.flag && d.flag[0] == L'!' && d.def && d.def[0] &&
                GetCtlText(PageBase(p) + i).empty())
                SetCtlText(PageBase(p) + i, d.def);
        }
    }
    return true;
}

// ---------------------------------------------------------------- settings actions
bool SaveSettings() {
    if (g_cfgPath.empty()) return false;
    return IniSave();
}

static void SelectComboText(HWND h, const wchar_t* text) {
    if (!h || !text || !text[0]) return;
    int n = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        wchar_t buf[512] = { 0 };
        SendMessageW(h, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
        if (wcscmp(buf, text) == 0) { SendMessageW(h, CB_SETCURSEL, (WPARAM)i, 0); return; }
    }
}

bool ResetSettings() {
    if (!g_cfgPath.empty()) DeleteFileW(g_cfgPath.c_str());
    for (int p = 1; p < g_optPageCount; p++) {
        for (int i = 0; i < g_optPages[p].count; i++) {
            int id = PageBase(p) + i;
            HWND h = Ctl(id);
            if (!h) continue;
            const OptDef& d = g_optPages[p].defs[i];
            if (d.flag && d.flag[0] == L'!') { /* app setting, reset below too */ }
            if (d.kind == OPT_CHECK) {
                bool on = d.def && wcscmp(d.def, L"1") == 0;
                SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
                InvalidateRect(h, nullptr, FALSE);
            } else if (d.kind == OPT_COMBO) {
                SendMessageW(h, CB_SETCURSEL, 0, 0);
                if (d.def && d.def[0]) SelectComboText(h, d.def);
            } else if (d.kind == OPT_EDIT || d.kind == OPT_FILE) {
                SetWindowTextW(h, d.def ? d.def : L"");
            }
        }
    }
    // Download page defaults
    SetComboSel(IDC_QUALITY, DefaultComboSel(IDC_QUALITY));
    SetComboSel(IDC_CONTAINER, DefaultComboSel(IDC_CONTAINER));
    SetCtlText(IDC_OUTDIR, L"");
    // Playlist / Audio page defaults
    SetComboSel(IDC_PL_QUALITY, DefaultComboSel(IDC_PL_QUALITY));
    SetComboSel(IDC_PL_CONTAINER, DefaultComboSel(IDC_PL_CONTAINER));
    SetCtlText(IDC_PL_OUTDIR, L"");
    SetCtlChecked(IDC_PL_FOLDER, false);
    SetCtlText(IDC_AUD_OUTDIR, L"");
    return true;
}

void LogFmt(const wchar_t* fmt, ...) {
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 4095, fmt, ap);
    va_end(ap);
    buf[4095] = 0;
    AppendLog(buf);
}
