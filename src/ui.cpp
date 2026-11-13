// yt-dlp GUI - Fluent Design controls, layout, scrolling
#include "app.h"
#include "fluent.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <cstdio>
#include <algorithm>

HINSTANCE g_hInst = nullptr;
HWND g_hMain = nullptr, g_hTip = nullptr;
HWND g_hPages[8] = { 0 };
HWND g_hNav[8] = { 0 };
HWND g_hScroll = nullptr;
int g_page = 0;
std::vector<Widget> g_widgets;
int g_scrollY[8] = { 0 };
int g_contentH[8] = { 0 };
static int g_scrollTarget[8] = { 0 };
static bool g_scrollTimerOn = false;

UINT g_dpi = 96;
HFONT g_fonts[5] = { 0 };
std::wstring g_exeDir, g_cfgPath;
bool g_micaOn = false;
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushPage = nullptr;
HBRUSH g_brushCard = nullptr;
HBRUSH g_brushField = nullptr;
HBRUSH g_brushLog = nullptr;
HBRUSH g_brushBorder = nullptr;

static std::vector<std::wstring*> g_tipTexts;
static const wchar_t* kPageClass = L"YtDlpGuiPage";
static const wchar_t* kComboClass = L"YtDlpCombo";
static const wchar_t* kComboPopupClass = L"YtDlpComboPopup";
static const wchar_t* kScrollClass = L"YtDlpScroll";

LRESULT CALLBACK PageWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void CloseComboPopupIfAny();

struct PageMetrics { int split; int colW; int pad; int gap; };

static void BuildDownloadPage(int p);
static void BuildPlaylistPage(int p);
static void BuildAudioPage(int p);
static void BuildOptPage(int p);
static void LayoutOnePage(int p, int pw, int ph);
static HWND MakeStatic(int id, int page, const wchar_t* text, int font, DWORD extra = 0);
static HWND MakeButton(int id, int page, const wchar_t* text, int font, bool primary, bool danger,
                       bool nav, bool check, COLORREF bg);
static HWND MakeEdit(int id, int page, const wchar_t* text, DWORD extra = 0, bool multiline = false);
static HWND MakeList(int id, int page, const wchar_t* const* heads, int n);
static void ComboItems(HWND h, const wchar_t* const* items, int n, int sel);
static void AddColumns(HWND lv, const wchar_t* const* heads, int n);

int Dpi(int v) { return MulDiv(v, (int)g_dpi, 96); }
int UnDpi(int v) { return g_dpi ? MulDiv(v, 96, (int)g_dpi) : v; }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void AddWidget(HWND h, int id, int page, int font) {
    if (!h) return;
    g_widgets.push_back({ h, id, page, font });
}

HWND Ctl(int id) {
    for (const auto& w : g_widgets)
        if (w.id == id) return w.h;
    return nullptr;
}

int PageOfControl(HWND h) {
    for (const auto& w : g_widgets)
        if (w.h == h) return w.page;
    return -1;
}

// ---------------------------------------------------------------- fonts
void RebuildFonts() {
    for (int i = 0; i < 5; i++)
        if (g_fonts[i]) { DeleteObject(g_fonts[i]); g_fonts[i] = nullptr; }
    const wchar_t* face = L"Segoe UI Variable Text";
    const wchar_t* display = L"Segoe UI Variable Display";
    g_fonts[FONT_BODY] = CreateFontW(-Dpi(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, face);
    g_fonts[FONT_BOLD] = CreateFontW(-Dpi(14), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, face);
    g_fonts[FONT_MONO] = CreateFontW(-Dpi(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     FIXED_PITCH, L"Consolas");
    g_fonts[FONT_SMALL] = CreateFontW(-Dpi(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH, face);
    g_fonts[FONT_TITLE] = CreateFontW(-Dpi(21), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH, display);
}

void ApplyFonts() {
    for (const auto& w : g_widgets) {
        int f = (w.font >= 0 && w.font < 5) ? w.font : 0;
        SendMessageW(w.h, WM_SETFONT, (WPARAM)g_fonts[f], TRUE);
    }
}

// ---------------------------------------------------------------- drawing helpers
static void FillRound(HDC dc, int left, int top, int right, int bottom, int radius,
                      COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, left, top, right, bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static COLORREF Mix(COLORREF a, COLORREF b, int pct) {
    int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * pct / 100;
    int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * pct / 100;
    int bl = GetBValue(a) + (GetBValue(b) - GetGValue(a)) * pct / 100;
    return RGB(r, g, bl);
}

static bool IsNavButton(int id) {
    return id == IDC_NAV_DOWNLOAD || id == IDC_NAV_SETTINGS;
}

struct BtnInfo {
    bool primary = false;
    bool nav = false;
    bool danger = false;
    bool check = false;
    bool checked = false;
    COLORREF bg = ColBg();
    bool hot = false;
};

bool IsCheckCtl(HWND h) {
    BtnInfo* bi = (BtnInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return bi && bi->check;
}

static void ToggleCheck(HWND h) {
    BtnInfo* bi = (BtnInfo*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!bi || !bi->check) return;
    bi->checked = !bi->checked;
    InvalidateRect(h, nullptr, FALSE);
}

static LRESULT CALLBACK BtnSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    BtnInfo* bi = (BtnInfo*)ref;
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (bi && !bi->hot) {
                bi->hot = true;
                TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (bi) { bi->hot = false; InvalidateRect(h, nullptr, FALSE); }
            break;
        case WM_LBUTTONUP:
            if (bi && bi->check) {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                RECT rc;
                GetClientRect(h, &rc);
                if (PtInRect(&rc, pt)) ToggleCheck(h);
            }
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_KEYDOWN:
            if (bi && bi->check && wp == VK_SPACE) {
                ToggleCheck(h);
                return 0;
            }
            InvalidateRect(h, nullptr, FALSE);
            break;
        case BM_SETCHECK:
            if (bi && bi->check) {
                bi->checked = (wp == BST_CHECKED);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case BM_GETCHECK:
            if (bi && bi->check) return bi->checked ? BST_CHECKED : BST_UNCHECKED;
            break;
        case WM_LBUTTONDOWN:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_NCDESTROY:
            if (bi) delete bi;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            RemoveWindowSubclass(h, BtnSubclass, id);
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static bool NavIsSelected(int id) {
    switch (id) {
        case IDC_NAV_DOWNLOAD: return g_page == 0;
        case IDC_NAV_PLAYLIST: return g_page == 1;
        case IDC_NAV_AUDIO: return g_page == 3;
        case IDC_NAV_SETTINGS: return g_page == g_optPageCount - 1;
        default: return false;
    }
}

bool DarkDrawItem(LPDRAWITEMSTRUCT dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return false;
    BtnInfo* bi = (BtnInfo*)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
    if (!bi) return false;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0 || GetFocus() == dis->hwndItem;

    RECT full = rc;
    FillRect(dc, &full, bi->bg == ColPage() ? g_brushPage :
                         bi->bg == ColCard() ? g_brushCard : g_brushBg);

    RECT box = rc;
    InflateRect(&box, -1, -1);

    if (bi->check) {
        bool checked = bi->checked;
        int side = Dpi(17);
        RECT b = { box.left + Dpi(2), (box.top + box.bottom - side) / 2,
                   box.left + Dpi(2) + side, (box.top + box.bottom - side) / 2 + side };
        COLORREF fill = checked ? ColAccent() : (bi->hot ? ColHover() : ColField());
        COLORREF border = checked ? ColAccent() : ColBorderHot();
        if (disabled) {
            fill = Mix(bi->bg, ColField(), 40);
            border = Mix(bi->bg, ColBorder(), 40);
        } else if (focused) {
            border = ColAccent();
        }
        FillRound(dc, b.left, b.top, b.right, b.bottom, Dpi(4), fill, border);
        if (checked) {
            HPEN pen = CreatePen(PS_SOLID, Dpi(2) < 1 ? 1 : Dpi(2), disabled ? ColMuted() : ColAccentText());
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, b.left + side * 26 / 100, b.top + side * 54 / 100, nullptr);
            LineTo(dc, b.left + side * 43 / 100, b.bottom - side * 24 / 100);
            LineTo(dc, b.right - side * 24 / 100, b.top + side * 26 / 100);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
        wchar_t textBuf[256] = { 0 };
        GetWindowTextW(dis->hwndItem, textBuf, 255);
        HGDIOBJ oldFont = SelectObject(dc, g_fonts[FONT_BODY]);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, disabled ? ColMuted() : ColText());
        RECT tr = box;
        tr.left = b.right + Dpi(9);
        tr.right -= Dpi(3);
        DrawTextW(dc, textBuf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        return true;
    }

    COLORREF fill = ColField();
    COLORREF border = ColBorder();
    COLORREF text = ColText();
    int radius = Dpi(7);

    if (disabled) {
        fill = Mix(bi->bg, ColField(), 40);
        border = Mix(bi->bg, ColBorder(), 40);
        text = ColMuted();
    } else if (bi->primary) {
        fill = pressed ? ColPressed() : bi->hot ? Mix(ColAccent(), RGB(255, 255, 255), 12) : ColAccent();
        border = fill;
        text = ColAccentText();
        radius = Dpi(7);
    } else if (bi->nav) {
        bool selected = NavIsSelected(dis->CtlID);
        fill = selected ? ColCard() : (pressed ? ColPressed() : bi->hot ? ColHover() : ColPage());
        border = fill;
        text = selected ? ColAccent() : (bi->hot ? ColAccent() : ColText());
        if (selected) {
            RECT bar = { box.left + Dpi(2), box.top + Dpi(9), box.left + Dpi(5), box.bottom - Dpi(9) };
            FillRound(dc, bar.left, bar.top, bar.right, bar.bottom, Dpi(2), ColAccent(), ColAccent());
        }
        radius = Dpi(6);
    } else if (bi->danger) {
        fill = pressed ? ColPressed() : bi->hot ? ColHover() : ColField();
        border = bi->hot ? ColBorderHot() : ColBorder();
        text = bi->hot ? ColText() : ColText();
    } else {
        fill = pressed ? ColPressed() : bi->hot ? ColHover() : ColField();
        border = bi->hot ? ColBorderHot() : ColBorder();
        text = ColText();
    }

    if (focused && !bi->primary) border = ColAccent();
    FillRound(dc, box.left, box.top, box.right, box.bottom, radius, fill, border);

    wchar_t textBuf[256] = { 0 };
    GetWindowTextW(dis->hwndItem, textBuf, 255);
    int fontIdx = (bi->primary || bi->nav) ? FONT_BOLD : FONT_BODY;
    if (bi->nav && !NavIsSelected(dis->CtlID)) fontIdx = FONT_BODY;
    HFONT font = g_fonts[fontIdx];
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    RECT tr = box;
    if (bi->nav) {
        tr.left += Dpi(18);
        tr.right -= Dpi(8);
        DrawTextW(dc, textBuf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        DrawTextW(dc, textBuf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
    return true;
}

// ---------------------------------------------------------------- edit border
static LRESULT CALLBACK EditBorderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    switch (msg) {
        case WM_NCPAINT: {
            HDC dc = GetWindowDC(h);
            if (dc) {
                RECT wr;
                GetWindowRect(h, &wr);
                RECT r = { 0, 0, wr.right - wr.left, wr.bottom - wr.top };
                HBRUSH br = CreateSolidBrush(GetFocus() == h ? ColAccent() : ColBorder());
                FrameRect(dc, &r, br);
                DeleteObject(br);
                ReleaseDC(h, dc);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            LRESULT r = DefSubclassProc(h, msg, wp, lp);
            RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
            return r;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static void StyleSingleLineEdit(HWND h) {
    SetWindowSubclass(h, EditBorderProc, 1, 0);
}

// ---------------------------------------------------------------- tooltip / placement
void TipAdd(HWND h, const std::wstring& text) {
    if (!g_hTip || text.empty() || !h) return;
    std::wstring* t = new std::wstring(text);
    g_tipTexts.push_back(t);
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(h);
    ti.uId = (UINT_PTR)h;
    ti.lpszText = &(*t)[0];
    SendMessageW(g_hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// Batch layout support
static HDWP g_hdwp = nullptr;

void PlaceBegin() {
    g_hdwp = BeginDeferWindowPos(64);
}

void Place(int id, int x, int y, int w, int h) {
    HWND hw = Ctl(id);
    if (!hw) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g_hdwp) {
        g_hdwp = DeferWindowPos(g_hdwp, hw, nullptr, Dpi(x), Dpi(y), Dpi(w), Dpi(h), SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hw, nullptr, Dpi(x), Dpi(y), Dpi(w), Dpi(h), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void PlaceEnd() {
    if (g_hdwp) {
        EndDeferWindowPos(g_hdwp);
        g_hdwp = nullptr;
    }
}

// ShowWindow on a control that is already in the requested state still sends
// WM_SHOWWINDOW and invalidates, and the layout runs on every resize step.
void ShowCtl(int id, bool show) {
    HWND h = Ctl(id);
    if (!h) return;
    bool visible = (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
    if (visible == show) return;
    ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}

// ---------------------------------------------------------------- control text helpers
std::wstring GetCtlText(int id) {
    HWND h = Ctl(id);
    if (!h) return std::wstring();
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return std::wstring();
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

void SetCtlText(int id, const std::wstring& s) {
    HWND h = Ctl(id);
    if (!h) return;
    // Resize steps re-run this many times per second; SetWindowText on an
    // unchanged string still re-measures the control and repaints it.
    wchar_t cur[512];
    int n = GetWindowTextW(h, cur, 512);
    if (n == (int)s.size() && wcsncmp(cur, s.c_str(), n) == 0) return;
    SetWindowTextW(h, s.c_str());
}

int ComboSel(int id) {
    HWND h = Ctl(id);
    return h ? (int)SendMessageW(h, CB_GETCURSEL, 0, 0) : 0;
}

void SetComboSel(int id, int i) {
    HWND h = Ctl(id);
    if (h) SendMessageW(h, CB_SETCURSEL, (WPARAM)i, 0);
}

// Out-of-the-box selections for the download combos. The lists are built in
// this order: quality 0=Best, 1=2160p, 2=1440p, 3=1080p, ...; container
// 0=Automatic, 1=MP4, 2=MKV, 3=WebM. Used both when the control is created and
// when a settings file has no saved value, so a fresh start gets 1080p/MP4
// while a saved choice is still restored.
int DefaultComboSel(int id) {
    switch (id) {
        case IDC_QUALITY:
        case IDC_PL_QUALITY: return 3;   // 1080p
        case IDC_CONTAINER:
        case IDC_PL_CONTAINER: return 1; // MP4
        default: return 0;
    }
}

bool CtlChecked(int id) {
    HWND h = Ctl(id);
    return h && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetCtlChecked(int id, bool v) {
    HWND h = Ctl(id);
    if (h) SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
}

void EnableCtl(int id, bool on) {
    HWND h = Ctl(id);
    if (h) EnableWindow(h, on ? TRUE : FALSE);
}

void SetPct(int pct) {
    HWND h = Ctl(IDC_PROGRESS);
    if (!h) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    SendMessageW(h, PBM_SETPOS, (WPARAM)(pct * 10), 0);
}

void SetStatus(const std::wstring& s) {
    HWND h = Ctl(IDC_STATUS);
    if (h) SetWindowTextW(h, s.c_str());
}

// ---------------------------------------------------------------- log
void AppendLog(const std::wstring& s) {
    HWND h = Ctl(IDC_LOG);
    if (!h) return;
    int len = GetWindowTextLengthW(h);
    if (len > 160000) {
        SendMessageW(h, EM_SETSEL, 0, 70000);
        SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(h);
    }
    SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    std::wstring txt = s;
    txt.push_back(L'\r');
    txt.push_back(L'\n');
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)txt.c_str());
}

void ClearLog() {
    HWND h = Ctl(IDC_LOG);
    if (h) SetWindowTextW(h, L"");
}

// ---------------------------------------------------------------- dark combo box
struct ComboState {
    std::vector<std::wstring> items;
    int sel = -1;
    int selBefore = -1;
    bool hot = false;
    bool open = false;
    HWND self = nullptr;
    HWND popup = nullptr;
    HFONT font = nullptr;
    COLORREF bg = ColPage();
};

struct ComboPopupState {
    HWND combo = nullptr;
    int hot = -1;
    int top = 0;
    int itemH = 28;
    int visible = 8;
    HFONT font = nullptr;
};

static HWND g_comboPopup = nullptr;

static ComboState* ComboOf(HWND h) {
    return (ComboState*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

static void ComboNotify(HWND combo) {
    HWND parent = GetParent(combo);
    if (parent)
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
}

static void ComboClosePopup(ComboState* st) {
    if (!st || !st->popup) return;
    HWND pop = st->popup;
    st->popup = nullptr;
    st->open = false;
    if (g_comboPopup == pop) g_comboPopup = nullptr;
    if (GetCapture() == pop) ReleaseCapture();
    DestroyWindow(pop);
    if (st->sel < 0 && !st->items.empty()) st->sel = 0;
    if (st->self) InvalidateRect(st->self, nullptr, FALSE);
}

static void CloseComboPopupIfAny() {
    if (!g_comboPopup) return;
    HWND pop = g_comboPopup;
    ComboPopupState* ps = (ComboPopupState*)GetWindowLongPtrW(pop, GWLP_USERDATA);
    if (ps) ComboClosePopup(ComboOf(ps->combo));
    else {
        g_comboPopup = nullptr;
        DestroyWindow(pop);
    }
}

static void ComboEnsureVisible(ComboPopupState* ps, const ComboState* st) {
    if (!ps || !st) return;
    if (st->sel < ps->top) ps->top = st->sel;
    if (st->sel >= ps->top + ps->visible) ps->top = st->sel - ps->visible + 1;
    if (ps->top < 0) ps->top = 0;
    int maxTop = (int)st->items.size() - ps->visible;
    if (maxTop < 0) maxTop = 0;
    if (ps->top > maxTop) ps->top = maxTop;
}

static void ComboOpenPopup(HWND combo, ComboState* st) {
    if (!st || st->items.empty() || st->open) return;
    if (g_comboPopup) {
        HWND old = g_comboPopup;
        g_comboPopup = nullptr;
        DestroyWindow(old);
    }
    st->selBefore = st->sel;
    RECT cr;
    GetWindowRect(combo, &cr);
    int itemH = Dpi(28);
    int visible = ClampI((int)st->items.size(), 1, 9);
    int width = cr.right - cr.left;
    int height = visible * itemH + Dpi(6);

    MONITORINFO mi = { sizeof(mi) };
    HMONITOR mon = MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    RECT work = mi.rcWork;
    int x = cr.left;
    int y = cr.bottom + Dpi(2);
    if (y + height > work.bottom) y = cr.top - Dpi(2) - height;
    if (y < work.top) y = work.top;
    if (x + width > work.right) x = work.right - width;
    if (x < work.left) x = work.left;

    HWND pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                               kComboPopupClass, L"", WS_POPUP, x, y, width, height,
                               g_hMain, nullptr, g_hInst, nullptr);
    if (!pop) return;
    ComboPopupState* ps = new ComboPopupState();
    ps->combo = combo;
    ps->itemH = itemH;
    ps->visible = visible;
    ps->font = st->font;
    ps->top = 0;
    if (st->sel >= visible) ps->top = st->sel - visible + 1;
    SetWindowLongPtrW(pop, GWLP_USERDATA, (LONG_PTR)ps);

    st->popup = pop;
    st->open = true;
    g_comboPopup = pop;
    FluentRoundedCorners(pop);
    FluentBorder(pop, ColBorder());
    ShowWindow(pop, SW_SHOWNA);
    UpdateWindow(pop);
    SetCapture(pop);
    InvalidateRect(combo, nullptr, FALSE);
}

static LRESULT CALLBACK ComboPopupProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    ComboPopupState* ps = (ComboPopupState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT p;
            HDC dc = BeginPaint(h, &p);
            RECT rc;
            GetClientRect(h, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(38, 38, 43));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            if (!ps) { EndPaint(h, &p); return 0; }
            ComboState* st = ComboOf(ps->combo);
            if (!st) { EndPaint(h, &p); return 0; }
            HFONT font = ps->font ? ps->font : g_fonts[FONT_BODY];
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            int count = (int)st->items.size();
            int padY = Dpi(3);
            for (int i = ps->top; i < count && i < ps->top + ps->visible; i++) {
                int row = i - ps->top;
                RECT r = { Dpi(4), padY + row * ps->itemH,
                           rc.right - Dpi(4), padY + (row + 1) * ps->itemH - Dpi(1) };
                if (i == st->sel) {
                    HBRUSH b = CreateSolidBrush(ColSel());
                    FillRect(dc, &r, b);
                    DeleteObject(b);
                } else if (i == ps->hot) {
                    HBRUSH b = CreateSolidBrush(ColFieldHot());
                    FillRect(dc, &r, b);
                    DeleteObject(b);
                }
                RECT tr = r;
                tr.left += Dpi(12);
                tr.right -= Dpi(8);
                SetTextColor(dc, i == st->sel ? RGB(255, 255, 255) : ColText());
                DrawTextW(dc, st->items[(size_t)i].c_str(), -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(dc, oldFont);

            int maxTop = count - ps->visible;
            if (maxTop > 0) {
                int trackTop = Dpi(5), trackBottom = rc.bottom - Dpi(5);
                int trackH = trackBottom - trackTop;
                int thumbH = trackH * ps->visible / count;
                if (thumbH < Dpi(20)) thumbH = Dpi(20);
                if (thumbH > trackH) thumbH = trackH;
                int thumbY = trackTop + (trackH - thumbH) * ps->top / maxTop;
                RECT thumb = { rc.right - Dpi(6), thumbY, rc.right - Dpi(3), thumbY + thumbH };
                FillRound(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, Dpi(2), ColBorderHot(), ColBorderHot());
            }

            RECT border = rc;
            border.right -= 1;
            border.bottom -= 1;
            HBRUSH bb = CreateSolidBrush(ColBorder());
            FrameRect(dc, &border, bb);
            DeleteObject(bb);
            EndPaint(h, &p);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!ps) break;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int idx = -1;
            int y = pt.y - Dpi(3);
            if (y >= 0) {
                int row = y / ps->itemH;
                if (row >= 0 && row < ps->visible) idx = ps->top + row;
            }
            ComboState* st = ComboOf(ps->combo);
            if (st && (idx < 0 || idx >= (int)st->items.size())) idx = -1;
            if (ps->hot != idx) { ps->hot = idx; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!ps) break;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc;
            GetClientRect(h, &rc);
            int y = pt.y - Dpi(3);
            int row = y >= 0 ? y / ps->itemH : -1;
            ComboState* st = ComboOf(ps->combo);
            if (st && row >= 0 && row < ps->visible) {
                int idx = ps->top + row;
                if (idx >= 0 && idx < (int)st->items.size()) {
                    st->sel = idx;
                    ComboNotify(ps->combo);
                }
            }
            ComboClosePopup(st);
            return 0;
        }
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (ps) ComboClosePopup(ComboOf(ps->combo));
            return 0;
        case WM_MOUSEWHEEL:
            if (ps) {
                ComboState* st = ComboOf(ps->combo);
                if (st) {
                    int amount = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                    ps->top = ClampI(ps->top + amount, 0,
                                     (int)st->items.size() - ps->visible > 0
                                         ? (int)st->items.size() - ps->visible : 0);
                    InvalidateRect(h, nullptr, FALSE);
                }
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (ps && (HWND)lp != h) ComboClosePopup(ComboOf(ps->combo));
            return 0;
        case WM_DESTROY:
            if (ps) {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                delete ps;
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- playlist audio popup
// A small dark popup shown when the Playlist "Audio only (MP3)" toggle is
// clicked. Thumbnail and metadata embedding are a single mouse-clickable row
// (both change together); the OK button commits the choice and always closes
// the popup, even when the row was left untouched.
static const wchar_t* kAudioPopupClass = L"YtDlpAudioPopup";
static HWND g_audioPopup = nullptr;

struct AudioPopupState {
    bool embed = true;
    bool hotToggle = false;
    bool hotOk = false;
};

static void AudioPopupRects(HWND h, RECT* row, RECT* ok) {
    RECT rc;
    GetClientRect(h, &rc);
    int pad = Dpi(20);
    row->left = pad;
    row->right = rc.right - pad;
    row->top = Dpi(74);
    row->bottom = row->top + Dpi(48);
    int bw = Dpi(104), bh = Dpi(34);
    ok->right = rc.right - pad;
    ok->left = ok->right - bw;
    ok->bottom = rc.bottom - pad;
    ok->top = ok->bottom - bh;
}

static void AudioPopupPaint(HWND h, AudioPopupState* st) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int pad = Dpi(20);

    FillRound(dc, 0, 0, rc.right, rc.bottom, Dpi(8), ColPage(), ColBorder());

    HGDIOBJ oldFont = SelectObject(dc, g_fonts[FONT_BOLD]);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ColText());
    RECT tr = { pad, Dpi(16), rc.right - pad, Dpi(40) };
    DrawTextW(dc, L"Audio only (MP3)", -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, g_fonts[FONT_SMALL]);
    SetTextColor(dc, ColMuted());
    RECT sr = { pad, Dpi(42), rc.right - pad, Dpi(62) };
    DrawTextW(dc, L"Choose how playlist audio downloads are saved.", -1, &sr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT row, ok;
    AudioPopupRects(h, &row, &ok);

    FillRound(dc, row.left, row.top, row.right, row.bottom, Dpi(6),
              st->hotToggle ? ColFieldHot() : ColField(),
              st->hotToggle ? ColBorderHot() : ColBorder());

    int box = Dpi(22);
    int boxTop = row.top + (row.bottom - row.top - box) / 2;
    RECT cb = { row.left + Dpi(14), boxTop, row.left + Dpi(14) + box, boxTop + box };
    if (st->embed) {
        FillRound(dc, cb.left, cb.top, cb.right, cb.bottom, Dpi(5), ColAccent(), ColAccent());
        HPEN pen = CreatePen(PS_SOLID, Dpi(2), RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        POINT check[3] = {
            { cb.left + Dpi(5), cb.top + Dpi(11) },
            { cb.left + Dpi(9), cb.top + Dpi(16) },
            { cb.right - Dpi(5), cb.top + Dpi(6) }
        };
        Polyline(dc, check, 3);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    } else {
        FillRound(dc, cb.left, cb.top, cb.right, cb.bottom, Dpi(5), ColBg(), ColBorderHot());
    }

    SelectObject(dc, g_fonts[FONT_BODY]);
    SetTextColor(dc, ColText());
    RECT lr = { cb.right + Dpi(12), row.top, row.right - Dpi(14), row.bottom };
    DrawTextW(dc, L"Embed thumbnail and metadata", -1, &lr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, g_fonts[FONT_SMALL]);
    SetTextColor(dc, ColMuted());
    RECT hr = { row.left + Dpi(2), row.bottom + Dpi(5), row.right, row.bottom + Dpi(23) };
    DrawTextW(dc, L"Both options are applied together. Click OK to confirm.", -1, &hr,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    FillRound(dc, ok.left, ok.top, ok.right, ok.bottom, Dpi(5),
              st->hotOk ? RGB(0, 144, 255) : ColAccent(), ColAccent());
    SelectObject(dc, g_fonts[FONT_BOLD]);
    SetTextColor(dc, ColAccentText());
    RECT okText = ok;
    DrawTextW(dc, L"OK", -1, &okText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, oldFont);
    EndPaint(h, &ps);
}

static void CloseAudioPopup() {
    HWND pop = g_audioPopup;
    g_audioPopup = nullptr;
    if (pop) DestroyWindow(pop);
}

static LRESULT CALLBACK AudioPopupProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    AudioPopupState* st = (AudioPopupState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (st) AudioPopupPaint(h, st);
            else {
                PAINTSTRUCT p;
                HDC dc = BeginPaint(h, &p);
                RECT rc;
                GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brushPage);
                EndPaint(h, &p);
            }
            return 0;
        case WM_MOUSEMOVE: {
            if (!st) break;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT row, ok;
            AudioPopupRects(h, &row, &ok);
            bool ht = PtInRect(&row, pt) != FALSE;
            bool ho = PtInRect(&ok, pt) != FALSE;
            if (ht != st->hotToggle || ho != st->hotOk) {
                st->hotToggle = ht;
                st->hotOk = ho;
                TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (st) {
                st->hotToggle = false;
                st->hotOk = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (!st) break;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT row, ok;
            AudioPopupRects(h, &row, &ok);
            SetFocus(h);
            if (PtInRect(&row, pt)) {
                st->embed = !st->embed;
                InvalidateRect(h, nullptr, FALSE);
            } else if (PtInRect(&ok, pt)) {
                g_playlistEmbed = st->embed;
                CloseAudioPopup();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (!st) break;
            if (wp == VK_ESCAPE) {
                CloseAudioPopup();
                return 0;
            }
            if (wp == VK_RETURN) {
                g_playlistEmbed = st->embed;
                CloseAudioPopup();
                return 0;
            }
            if (wp == VK_SPACE) {
                st->embed = !st->embed;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CLOSE:
            CloseAudioPopup();
            return 0;
        case WM_DESTROY:
            if (g_audioPopup == h) g_audioPopup = nullptr;
            if (st) {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                delete st;
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void RegisterAudioPopupClass() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = AudioPopupProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kAudioPopupClass;
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
}

void ShowPlaylistAudioPopup() {
    if (!g_hMain) return;
    // A second click on the toggle replaces the popup instead of stacking one.
    CloseAudioPopup();
    RegisterAudioPopupClass();

    RECT mr;
    GetWindowRect(g_hMain, &mr);
    int w = Dpi(440), h = Dpi(214);
    int x = mr.left + ((mr.right - mr.left) - w) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - h) / 2;
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR mon = MonitorFromWindow(g_hMain, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, &mi)) {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    }

    HWND pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kAudioPopupClass, L"",
                               WS_POPUP, x, y, w, h, g_hMain, nullptr, g_hInst, nullptr);
    if (!pop) return;
    AudioPopupState* st = new AudioPopupState();
    st->embed = g_playlistEmbed;
    SetWindowLongPtrW(pop, GWLP_USERDATA, (LONG_PTR)st);
    g_audioPopup = pop;
    FluentRoundedCorners(pop);
    FluentBorder(pop, ColBorder());
    ShowWindow(pop, SW_SHOW);
    SetForegroundWindow(pop);
    SetFocus(pop);
    UpdateWindow(pop);
}

static void ComboPaint(HWND h, ComboState* st) {
    PAINTSTRUCT p;
    HDC dc = BeginPaint(h, &p);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, st->bg == ColBg() ? g_brushBg : g_brushPage);

    RECT box = rc;
    InflateRect(&box, -1, -1);
    bool enabled = IsWindowEnabled(h) != FALSE;
    bool focus = GetFocus() == h || st->open;
    COLORREF fill = enabled ? ColField() : Mix(ColField(), st->bg, 55);
    COLORREF border = !enabled ? ColBorder() : focus ? ColAccent() : st->hot ? ColBorderHot() : ColBorder();
    FillRound(dc, box.left, box.top, box.right, box.bottom, Dpi(6), fill, border);

    HFONT font = st->font ? st->font : g_fonts[FONT_BODY];
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? ColText() : ColMuted());
    RECT tr = box;
    tr.left += Dpi(11);
    tr.right -= Dpi(30);
    const wchar_t* txt = L"(default)";
    if (st->sel >= 0 && st->sel < (int)st->items.size()) txt = st->items[(size_t)st->sel].c_str();
    DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    int cx = box.right - Dpi(14);
    int cy = (box.top + box.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, Dpi(1) < 1 ? 1 : Dpi(1), focus ? ColAccent() : ColMuted());
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, cx - Dpi(4), cy - Dpi(2), nullptr);
    LineTo(dc, cx, cy + Dpi(2));
    LineTo(dc, cx + Dpi(4), cy - Dpi(2));
    SelectObject(dc, op);
    DeleteObject(pen);
    SelectObject(dc, oldFont);
    EndPaint(h, &p);
}

static LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        ComboState* st = new ComboState();
        st->self = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        return 0;
    }
    ComboState* st = ComboOf(h);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (st) ComboPaint(h, st);
            else {
                PAINTSTRUCT p;
                BeginPaint(h, &p);
                EndPaint(h, &p);
            }
            return 0;
        case WM_SETFONT:
            if (st) { st->font = (HFONT)wp; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case WM_GETFONT:
            return st && st->font ? (LRESULT)st->font : 0;
        case WM_MOUSEMOVE:
            if (st && !st->hot) {
                st->hot = true;
                TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (st) { st->hot = false; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONDOWN:
            if (!st) break;
            if (st->open) ComboClosePopup(st);
            else {
                SetFocus(h);
                ComboOpenPopup(h, st);
            }
            return 0;
        case WM_KILLFOCUS:
            if (st && st->open) ComboClosePopup(st);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETFOCUS:
        case WM_ENABLE:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            if (!st) break;
            if (st->open) {
                ComboPopupState* ps = st->popup ? (ComboPopupState*)GetWindowLongPtrW(st->popup, GWLP_USERDATA) : nullptr;
                switch (wp) {
                    case VK_UP:
                        if (st->sel > 0) st->sel--;
                        if (ps) { ComboEnsureVisible(ps, st); InvalidateRect(st->popup, nullptr, FALSE); }
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    case VK_DOWN:
                        if (st->sel + 1 < (int)st->items.size()) st->sel++;
                        if (ps) { ComboEnsureVisible(ps, st); InvalidateRect(st->popup, nullptr, FALSE); }
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    case VK_HOME:
                        st->sel = 0;
                        if (ps) { ComboEnsureVisible(ps, st); InvalidateRect(st->popup, nullptr, FALSE); }
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    case VK_END:
                        st->sel = (int)st->items.size() - 1;
                        if (ps) { ComboEnsureVisible(ps, st); InvalidateRect(st->popup, nullptr, FALSE); }
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    case VK_RETURN:
                    case VK_SPACE:
                        ComboNotify(h);
                        ComboClosePopup(st);
                        return 0;
                    case VK_ESCAPE:
                        st->sel = st->selBefore;
                        ComboClosePopup(st);
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                }
            } else {
                if (wp == VK_DOWN || wp == VK_UP) {
                    int d = (wp == VK_DOWN) ? 1 : -1;
                    if (st->sel < 0) st->sel = 0;
                    else st->sel = ClampI(st->sel + d, 0, (int)st->items.size() - 1);
                    ComboNotify(h);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (wp == VK_RETURN || wp == VK_SPACE || (wp == VK_DOWN && GetKeyState(VK_MENU) < 0)) {
                    ComboOpenPopup(h, st);
                    return 0;
                }
            }
            break;
        case WM_MOUSEWHEEL:
            if (st && !st->items.empty()) {
                int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                if (st->sel < 0) st->sel = 0;
                else st->sel = ClampI(st->sel + d, 0, (int)st->items.size() - 1);
                ComboNotify(h);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case CB_ADDSTRING: {
            if (!st) return CB_ERR;
            st->items.push_back(lp ? (const wchar_t*)lp : L"");
            if (st->sel < 0) st->sel = 0;
            InvalidateRect(h, nullptr, FALSE);
            return (LRESULT)(st->items.size() - 1);
        }
        case CB_RESETCONTENT:
            if (st) { st->items.clear(); st->sel = -1; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case CB_SETCURSEL:
            if (!st) return CB_ERR;
            st->sel = (int)wp;
            if (st->sel >= (int)st->items.size()) st->sel = (int)st->items.size() - 1;
            if (st->sel < -1) st->sel = -1;
            InvalidateRect(h, nullptr, FALSE);
            return st->sel;
        case CB_GETCURSEL:
            return st ? st->sel : CB_ERR;
        case CB_GETCOUNT:
            return st ? (LRESULT)st->items.size() : 0;
        case CB_GETLBTEXT: {
            if (!st) return CB_ERR;
            int i = (int)wp;
            if (i < 0 || i >= (int)st->items.size() || !lp) return CB_ERR;
            const wchar_t* s = st->items[(size_t)i].c_str();
            wchar_t* out = (wchar_t*)lp;
            size_t k = 0;
            while (s[k]) { out[k] = s[k]; k++; }
            out[k] = 0;
            return (LRESULT)k;
        }
        case CB_GETLBTEXTLEN: {
            if (!st) return CB_ERR;
            int i = (int)wp;
            if (i < 0 || i >= (int)st->items.size()) return CB_ERR;
            return (LRESULT)st->items[(size_t)i].size();
        }
        case CB_SHOWDROPDOWN:
            if (!st) return 0;
            if (wp) ComboOpenPopup(h, st);
            else ComboClosePopup(st);
            return 0;
        case WM_NCDESTROY: {
            if (st) {
                if (st->popup) {
                    HWND pop = st->popup;
                    st->popup = nullptr;
                    if (g_comboPopup == pop) g_comboPopup = nullptr;
                    DestroyWindow(pop);
                }
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                delete st;
            }
            break;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void RegisterComboClasses() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = ComboProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kComboClass;
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ComboPopupProc;
    wc.lpszClassName = kComboPopupClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
}

static HWND MakeCombo(int id, int page) {
    HWND parent = page >= 0 ? g_hPages[page] : g_hMain;
    HWND h = CreateWindowExW(0, kComboClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    AddWidget(h, id, page, FONT_BODY);
    ComboState* st = ComboOf(h);
    if (st) st->bg = page >= 0 ? ColPage() : ColBg();
    return h;
}

static void ComboFill(HWND h, const wchar_t* items, bool addDefault) {
    if (addDefault) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"(default)");
    std::wstring cur;
    if (items) {
        for (const wchar_t* p = items; *p; p++) {
            if (*p == L'|') {
                if (!cur.empty()) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)cur.c_str());
                cur.clear();
            } else {
                cur.push_back(*p);
            }
        }
        if (!cur.empty()) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)cur.c_str());
    }
    SendMessageW(h, CB_SETCURSEL, 0, 0);
}

// ---------------------------------------------------------------- dark list views
static const int kDarkLists[] = { IDC_PL_LIST };

static HWND ListByHwnd(HWND h, bool* header) {
    for (int id : kDarkLists) {
        HWND lv = Ctl(id);
        if (!lv) continue;
        if (lv == h) { if (header) *header = false; return lv; }
        HWND hdr = (HWND)SendMessageW(lv, LVM_GETHEADER, 0, 0);
        if (hdr && hdr == h) { if (header) *header = true; return lv; }
    }
    return nullptr;
}

bool IsDarkListControl(HWND h) {
    return ListByHwnd(h, nullptr) != nullptr;
}

// The report view fills a selected row with the system highlight colour, which
// is a light grey while the window is inactive and the system accent while it
// is active. On this dark surface that made the Title/Status text of the
// clicked row disappear until the row happened to be repainted. Rows are
// therefore painted here in full: background, checkbox and every cell.
static void DrawDarkListRow(HWND lv, HDC dc, int item, RECT row) {
    RECT client;
    GetClientRect(lv, &client);
    if (row.left < client.left) row.left = client.left;
    if (row.right < client.right) row.right = client.right;

    bool sel = (ListView_GetItemState(lv, item, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(sel ? ColSel() : ColPage());
    FillRect(dc, &row, bg);
    DeleteObject(bg);

    int x = row.left;
    HWND hdr = (HWND)SendMessageW(lv, LVM_GETHEADER, 0, 0);
    int cols = hdr ? (int)SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0) : 0;

    // Checkbox: LVS_EX_CHECKBOXES puts the state image at the left of the row.
    int side = (row.bottom - row.top) - Dpi(12);
    if (side < Dpi(13)) side = Dpi(13);
    if (side > Dpi(16)) side = Dpi(16);
    int sideY = (row.top + row.bottom - side) / 2;
    RECT box = { x + Dpi(4), sideY, x + Dpi(4) + side, sideY + side };
    UINT stateImg = (UINT)ListView_GetItemState(lv, item, LVIS_STATEIMAGEMASK);
    bool checked = ((stateImg >> 12) & 0xF) == 2;
    FillRound(dc, box.left, box.top, box.right, box.bottom, Dpi(3),
              checked ? ColAccent() : ColField(),
              checked ? ColAccent() : ColBorderHot());
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, Dpi(2) < 1 ? 1 : Dpi(2), ColAccentText());
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, box.left + side * 26 / 100, box.top + side * 54 / 100, nullptr);
        LineTo(dc, box.left + side * 43 / 100, box.bottom - side * 24 / 100);
        LineTo(dc, box.right - side * 24 / 100, box.top + side * 26 / 100);
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    HFONT font = g_fonts[FONT_BODY] ? g_fonts[FONT_BODY] : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, sel ? RGB(255, 255, 255) : ColText());

    for (int i = 0; i < cols; i++) {
        int cw = ListView_GetColumnWidth(lv, i);
        if (cw < 0) cw = 0;
        RECT tr = row;
        tr.left = x + (i == 0 ? side + Dpi(11) : Dpi(8));
        tr.right = x + cw - Dpi(6);
        if (tr.right > tr.left) {
            wchar_t buf[512] = { 0 };
            LVITEMW it = { 0 };
            it.iSubItem = i;
            it.pszText = buf;
            it.cchTextMax = 512;
            SendMessageW(lv, LVM_GETITEMTEXTW, (WPARAM)item, (LPARAM)&it);
            DrawTextW(dc, buf, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        x += cw;
    }
    SelectObject(dc, oldFont);
}

LRESULT DarkListCustomDraw(LPARAM lp) {
    LPNMHDR nh = (LPNMHDR)lp;
    if (!nh) return CDRF_DODEFAULT;
    bool header = false;
    HWND lv = ListByHwnd(nh->hwndFrom, &header);
    if (!lv) return CDRF_DODEFAULT;
    LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;

    if (!header) {
        switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT: {
                // Only fill the area this paint actually covers. The list also
                // paints single items on its own (for example the row under the
                // pointer), and those paints arrive here with an empty
                // nmcd.rc. Filling the whole client then wiped every row and
                // the column header until something forced a full redraw.
                RECT client;
                GetClientRect(lv, &client);
                RECT rc = cd->nmcd.rc;
                if (rc.left < client.left) rc.left = client.left;
                if (rc.top < client.top) rc.top = client.top;
                if (rc.right > client.right) rc.right = client.right;
                if (rc.bottom > client.bottom) rc.bottom = client.bottom;
                if (rc.right > rc.left && rc.bottom > rc.top)
                    FillRect(cd->nmcd.hdc, &rc, g_brushPage);
                return CDRF_NOTIFYITEMDRAW;
            }
            case CDDS_ITEMPREPAINT: {
                RECT rc = cd->nmcd.rc;
                if (rc.right <= rc.left || rc.bottom <= rc.top) return CDRF_SKIPDEFAULT;
                DrawDarkListRow(lv, cd->nmcd.hdc, (int)cd->nmcd.dwItemSpec, rc);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    return CDRF_DODEFAULT;
}

// The header is a child of the ListView, so its NM_CUSTOMDRAW notification
// never reaches the page. Intercept it in the ListView subclass and paint the
// header with our own dark colours (otherwise Windows draws a white header).
static LRESULT CALLBACK DarkListSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND) {
        // Erase only the region being erased. Filling the entire client here
        // also erased the rows (and the header strip) during the list's own
        // partial repaints, which left the table blank.
        RECT rc;
        if (GetClipBox((HDC)wp, &rc) == ERROR) GetClientRect(h, &rc);
        if (rc.right > rc.left && rc.bottom > rc.top)
            FillRect((HDC)wp, &rc, g_brushPage);
        return 1;
    }
    if (msg == WM_NOTIFY) {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh && nh->code == NM_CUSTOMDRAW) {
            HWND hdr = (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0);
            if (hdr && nh->hwndFrom == hdr) {
                LRESULT r = DarkListCustomDraw(lp);
                if (r != CDRF_DODEFAULT) return r;
            }
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// Header windows paint themselves with the light system theme even when the
// list is dark, so the column bar is owner-painted here.
//
// The custom draw path is not enough: LVS_EX_DOUBLEBUFFER makes the list view
// render its children (the header included) through WM_PRINTCLIENT. Without an
// explicit WM_PRINTCLIENT handler the header falls back to the light system
// painting for a frame, which is what made the Title/Status column bar blink
// out and back while the list was being clicked or repainted.
static void PaintDarkHeader(HWND h, HDC dc) {
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, g_brushCard);

    HFONT f = g_fonts[FONT_BODY] ? g_fonts[FONT_BODY] : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ oldFont = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ColMuted());
    int n = (int)SendMessageW(h, HDM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        RECT r = { 0 };
        SendMessageW(h, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
        wchar_t buf[256] = { 0 };
        HDITEMW it = { 0 };
        it.mask = HDI_TEXT;
        it.pszText = buf;
        it.cchTextMax = 255;
        SendMessageW(h, HDM_GETITEMW, (WPARAM)i, (LPARAM)&it);
        RECT tr = r;
        tr.left += Dpi(9);
        tr.right -= Dpi(6);
        DrawTextW(dc, buf, -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (i + 1 < n) {
            HPEN pen = CreatePen(PS_SOLID, 1, ColBorder());
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, r.right - 1, r.top + Dpi(6), nullptr);
            LineTo(dc, r.right - 1, r.bottom - Dpi(6));
            SelectObject(dc, op);
            DeleteObject(pen);
        }
    }
    SelectObject(dc, oldFont);

    HPEN pen = CreatePen(PS_SOLID, 1, ColBorder());
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, 0, rc.bottom - 1, nullptr);
    LineTo(dc, rc.right, rc.bottom - 1);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static LRESULT CALLBACK HeaderPaintSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        if (GetClipBox((HDC)wp, &rc) == ERROR) GetClientRect(h, &rc);
        if (rc.right > rc.left && rc.bottom > rc.top)
            FillRect((HDC)wp, &rc, g_brushCard);
        return 1;
    }
    if (msg == WM_PRINTCLIENT) {
        // Sent by the list view's double buffer and by PrintWindow.
        PaintDarkHeader(h, (HDC)wp);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        // Draw into an off-screen buffer so a partial invalidation (clicking a
        // column, dragging one) never shows a half-painted column bar.
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
        if (mem && bmp) {
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            PaintDarkHeader(h, mem);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
        } else {
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            PaintDarkHeader(h, dc);
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------- custom scrollbar
static bool g_sbDrag = false;
static int g_sbGrab = 0;

static void ScrollbarThumb(HWND h, RECT* thumb, int* maxScroll) {
    RECT rc;
    GetClientRect(h, &rc);
    HWND page = g_hPages[g_page];
    if (!page) { SetRectEmpty(thumb); *maxScroll = 0; return; }
    RECT pr;
    GetClientRect(page, &pr);
    int contentPx = Dpi(g_contentH[g_page]);
    int viewPx = pr.bottom;
    int trackTop = Dpi(4);
    int trackBottom = rc.bottom - Dpi(4);
    if (trackBottom <= trackTop + 8) { SetRectEmpty(thumb); *maxScroll = 0; return; }
    int trackH = trackBottom - trackTop;
    int thumbH = contentPx > 0 ? trackH * viewPx / contentPx : trackH;
    int minThumb = Dpi(28);
    if (thumbH < minThumb) thumbH = minThumb;
    if (thumbH > trackH) thumbH = trackH;
    int maxOff = g_contentH[g_page] - UnDpi(viewPx);
    if (maxOff < 0) maxOff = 0;
    *maxScroll = maxOff;
    int thumbY = trackTop;
    if (maxOff > 0) thumbY = trackTop + (trackH - thumbH) * g_scrollY[g_page] / maxOff;
    thumb->left = Dpi(3);
    thumb->right = rc.right - Dpi(3);
    thumb->top = thumbY;
    thumb->bottom = thumbY + thumbH;
}

static LRESULT CALLBACK ScrollProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, g_brushBg);
            RECT thumb;
            int maxScroll = 0;
            ScrollbarThumb(h, &thumb, &maxScroll);
            if (!IsRectEmpty(&thumb)) {
                RECT track = { Dpi(4), Dpi(4), rc.right - Dpi(4), rc.bottom - Dpi(4) };
                FillRound(dc, track.left, track.top, track.right, track.bottom, Dpi(2), ColCard(), ColCard());
                FillRound(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, Dpi(2),
                          g_sbDrag ? ColAccent() : ColBorderHot(),
                          g_sbDrag ? ColAccent() : ColBorderHot());
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetCapture(h);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT thumb;
            int maxScroll = 0;
            ScrollbarThumb(h, &thumb, &maxScroll);
            if (PtInRect(&thumb, pt)) {
                g_sbDrag = true;
                g_sbGrab = pt.y - thumb.top;
            } else {
                int dir = pt.y < thumb.top ? -1 : 1;
                HWND page = g_hPages[g_page];
                RECT pr = { 0 };
                if (page) GetClientRect(page, &pr);
                int pageAmt = UnDpi(pr.bottom) * 85 / 100;
                ScrollPageTo(g_page, g_scrollY[g_page] + dir * pageAmt);
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_sbDrag && GetCapture() == h) {
                RECT thumb;
                int maxScroll = 0;
                ScrollbarThumb(h, &thumb, &maxScroll);
                RECT rc;
                GetClientRect(h, &rc);
                int trackTop = Dpi(4);
                int trackH = rc.bottom - Dpi(8);
                int thumbH = thumb.bottom - thumb.top;
                int span = trackH - thumbH;
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                int off = 0;
                if (span > 0 && maxScroll > 0) {
                    int y = pt.y - g_sbGrab - trackTop;
                    if (y < 0) y = 0;
                    if (y > span) y = span;
                    off = y * maxScroll / span;
                }
                ScrollPageTo(g_page, off);
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_sbDrag) {
                g_sbDrag = false;
                if (GetCapture() == h) ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            AnimateScrollBy(g_page, -d * Dpi(52) / WHEEL_DELTA);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- page window classes
static void RegisterPageClasses() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = PageWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPageClass;
    wc.style = 0;   // expose-only invalidation: no full page repaint on resize
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ScrollProc;
    wc.lpszClassName = kScrollClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
}

static int PageIndex(HWND h) {
    for (int i = 0; i < g_optPageCount; i++)
        if (g_hPages[i] == h) return i;
    return -1;
}

int PageAtPoint(HWND h) {
    while (h) {
        int p = PageIndex(h);
        if (p >= 0) return p;
        h = GetParent(h);
    }
    return -1;
}

LRESULT CALLBACK PageWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int p = PageIndex(h);
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)wp, &rc, g_brushPage);
            return 1;
        }
        case WM_PAINT: {
            // The page paints its own background so a strip that the slide
            // uncovers is never left showing whatever was underneath it.
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, g_brushPage);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (p < 0) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            AnimateScrollBy(p, -delta * Dpi(52) / WHEEL_DELTA);
            return 0;
        }
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return SendMessageW(g_hMain, msg, wp, lp);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- list columns
// True while a layout that must land on exact column widths is running (page
// switch, end of a drag). Normal resize steps skip tiny width changes: a list
// column resize costs a few milliseconds each, and a dozen of them per mouse
// move would make fast window drags feel like they are catching up.
static bool g_colForce = false;

static void SetColPercents(HWND lv, const int* pct, int n, int widthLogical) {
    if (!lv || n <= 0) return;
    // Skip redundant work while scrolling; column changes make list headers flash.
    static const wchar_t* kColProp = L"ytdlp_colw";
    static const wchar_t* kPhysProp = L"ytdlp_colphys";
    INT_PTR stamp = ((INT_PTR)widthLogical << 16) | (INT_PTR)(g_dpi & 0xFFFF);
    if ((INT_PTR)GetPropW(lv, kColProp) == stamp + 1) return;
    // Work in physical pixels and leave room for a vertical scrollbar so the
    // list never grows a horizontal scrollbar of its own.
    int phys = Dpi(widthLogical) - Dpi(18);
    if (phys < Dpi(120)) phys = Dpi(120);
    if (!g_colForce) {
        INT_PTR old = (INT_PTR)GetPropW(lv, kPhysProp);
        int delta = (int)old - phys;
        if (delta < 0) delta = -delta;
        if (old && delta < Dpi(14)) return;
    }
    SetPropW(lv, kPhysProp, (HANDLE)(INT_PTR)phys);
    SetPropW(lv, kColProp, (HANDLE)(stamp + 1));
    int total = 0;
    for (int i = 0; i < n; i++) total += pct[i];
    if (total <= 0) total = 1;
    int used = 0;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < n; i++) {
        int w = (i == n - 1) ? phys - used : phys * pct[i] / total;
        if (w < Dpi(30)) w = Dpi(30);
        used += w;
        SendMessageW(lv, LVM_SETCOLUMNWIDTH, (WPARAM)i, (LPARAM)w);
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, FALSE);
}

// ---------------------------------------------------------------- download page builder
static void BuildDownloadPage(int p) {
    MakeStatic(IDC_LBL_QUALITY, p, L"Quality", FONT_SMALL);
    static const wchar_t* quality[] = {
        L"Best available", L"2160p (4K)", L"1440p (2K)", L"1080p (Full HD)", L"720p (HD)",
        L"480p", L"360p", L"240p"
    };
    HWND q = MakeCombo(IDC_QUALITY, p);
    ComboItems(q, quality, (int)(sizeof(quality) / sizeof(quality[0])), DefaultComboSel(IDC_QUALITY));

    MakeStatic(IDC_LBL_CONT, p, L"Container", FONT_SMALL);
    static const wchar_t* containers[] = { L"Automatic", L"MP4", L"MKV", L"WebM" };
    HWND ct = MakeCombo(IDC_CONTAINER, p);
    ComboItems(ct, containers, (int)(sizeof(containers) / sizeof(containers[0])), DefaultComboSel(IDC_CONTAINER));

    MakeStatic(IDC_LBL_OUTDIR, p, L"Output folder", FONT_SMALL);
    MakeEdit(IDC_OUTDIR, p, L"");
    SendMessageW(Ctl(IDC_OUTDIR), EM_SETCUEBANNER, TRUE, (LPARAM)L"Default: the app folder");
    MakeButton(IDC_BROWSEOUT, p, L"Browse", FONT_BODY, false, false, false, false, ColCard());

    TipAdd(Ctl(IDC_QUALITY), L"Target height and stream merge strategy.");
    TipAdd(Ctl(IDC_CONTAINER), L"Container used when separate streams are merged.");
    TipAdd(Ctl(IDC_OUTDIR), L"Folder where finished files are saved.");
    TipAdd(Ctl(IDC_BROWSEOUT), L"Choose the download folder.");
}

// ---------------------------------------------------------------- playlist page builder
static void BuildPlaylistPage(int p) {
    MakeButton(IDC_PL_SELECT_ALL, p, L"Select All", FONT_BODY, false, false, false, false, ColCard());
    MakeButton(IDC_PL_SELECT_NONE, p, L"Select None", FONT_BODY, false, false, false, false, ColCard());

    // Video quality options for playlist downloads
    MakeStatic(IDC_PL_LBL_QUALITY, p, L"Video Quality", FONT_SMALL);
    static const wchar_t* quality[] = {
        L"Best available", L"2160p (4K)", L"1440p (2K)", L"1080p (Full HD)", L"720p (HD)",
        L"480p", L"360p", L"240p"
    };
    HWND q = MakeCombo(IDC_PL_QUALITY, p);
    ComboItems(q, quality, (int)(sizeof(quality) / sizeof(quality[0])), DefaultComboSel(IDC_PL_QUALITY));

    MakeStatic(IDC_PL_LBL_CONT, p, L"Container", FONT_SMALL);
    static const wchar_t* containers[] = { L"Automatic", L"MP4", L"MKV", L"WebM" };
    HWND ct = MakeCombo(IDC_PL_CONTAINER, p);
    ComboItems(ct, containers, (int)(sizeof(containers) / sizeof(containers[0])), DefaultComboSel(IDC_PL_CONTAINER));

    static const wchar_t* plHeads[] = { L"#", L"Title", L"Duration", L"ID", L"Status" };
    HWND lv = MakeList(IDC_PL_LIST, p, plHeads, 5);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

    MakeButton(IDC_PL_FOLDER, p, L"Create folder per playlist", FONT_BODY, false, false, false, true, ColPage());
    // "Audio only" is a toggle, sitting right next to the folder toggle: when
    // checked, Download playlist saves every entry as MP3 (normal quality).
    MakeButton(IDC_PL_AUDIO, p, L"Audio only (MP3)", FONT_BODY, false, false, false, true, ColPage());

    MakeStatic(IDC_PL_LBL_OUTDIR, p, L"Output folder", FONT_SMALL);
    MakeEdit(IDC_PL_OUTDIR, p, L"");
    MakeButton(IDC_PL_BROWSE, p, L"Browse", FONT_BODY, false, false, false, false, ColCard());

    TipAdd(Ctl(IDC_PL_SELECT_ALL), L"Check every playlist entry.");
    TipAdd(Ctl(IDC_PL_SELECT_NONE), L"Uncheck every playlist entry.");
    TipAdd(Ctl(IDC_PL_FOLDER), L"Create a subfolder named after the playlist.");
    TipAdd(Ctl(IDC_PL_AUDIO), L"When checked, Download playlist saves every checked entry as MP3 audio only, at normal quality. Clicking it also opens a small options popup where thumbnail and metadata embedding can be turned on or off together.");
    TipAdd(Ctl(IDC_PL_QUALITY), L"Target height and stream merge strategy for playlist downloads.");
    TipAdd(Ctl(IDC_PL_CONTAINER), L"Container used when separate streams are merged.");
}

// ---------------------------------------------------------------- audio page builder
static void BuildAudioPage(int p) {
    // The Audio section always saves MP3 with thumbnail and metadata embedded:
    // there is no format, bitrate or embedding choice here.
    MakeStatic(IDC_AUD_LBL_OUTDIR, p, L"Output folder", FONT_SMALL);
    MakeEdit(IDC_AUD_OUTDIR, p, L"");
    MakeButton(IDC_AUD_BROWSE, p, L"Browse", FONT_BODY, false, false, false, false, ColCard());

    TipAdd(Ctl(IDC_AUD_OUTDIR), L"Folder where finished audio files are saved.");
    TipAdd(Ctl(IDC_AUD_BROWSE), L"Choose the audio download folder.");
}

// ---------------------------------------------------------------- metrics
static const int kSidebarW = 212;
static const int kNavTop = 96;
static const int kNavH = 38;
static const int kNavGap = 2;
static const int kCardTop = 14;
static const int kCardUrlH = 64;    // paste-link row only
static const int kCardFullH = 112;  // paste-link row + download row
static const int kCardPad = 14;
static const int kPageGap = 12;
static const int kScrollW = 16;
static const int kLogH = 170;
static const int kDockW = 176;
static const int kDockH = 44;
static const int kDockGap = 12;
static const int kFetchW = 176;

// The link card is shared by the Video, Playlist and Audio sections. Keeping
// the same card height on all three means switching between them never moves
// the page or the shell at all: only the page content changes.
enum { CARD_NONE = 0, CARD_URL = 1, CARD_FULL = 2 };

static int CardModeForPage(int page) {
    if (page == 0 || page == 1 || page == 3) return CARD_URL;
    return CARD_NONE;   // Settings (and the unused option slots) have no card
}

struct UiMetrics {
    int w = 0, h = 0;
    int sbW = kSidebarW;
    int cardX = 0, cardY = 0, cardW = 0, cardH = 0;
    int pageX = 0, pageY = 0, pageW = 0, pageH = 0;
    int scrollX = 0, scrollY = 0, scrollW = 0, scrollH = 0;
    int logX = 0, logY = 0, logW = 0, logH = 0;
    int dockX = 0, dockY = 0, dockW = 0, dockH = 0;
};

static UiMetrics ComputeMetrics(int lw, int lh, int cardMode) {
    UiMetrics m;
    m.w = lw;
    m.h = lh;
    m.sbW = kSidebarW;
    m.cardX = m.sbW + 14;
    m.cardY = kCardTop;
    m.cardW = std::max(360, lw - m.cardX - 14);
    m.cardH = cardMode == CARD_FULL ? kCardFullH : (cardMode == CARD_URL ? kCardUrlH : 0);
    m.pageX = m.cardX;
    m.pageY = cardMode == CARD_NONE ? kCardTop : (m.cardY + m.cardH + kPageGap);
    m.logH = kLogH;
    m.logX = m.cardX;
    m.logY = lh - 14 - m.logH;
    // The log card spans the full width. The download row (and, on Playlist,
    // the fetch button) sits in its own band directly above the log card, with
    // the action buttons pinned to the right edge of that band.
    m.logW = m.cardW;
    m.dockW = kDockW;
    m.dockH = kDockH;
    m.dockX = m.cardX + m.cardW - m.dockW;
    m.dockY = m.logY - kDockGap - m.dockH;
    bool hasDock = cardMode != CARD_NONE;
    int pageBottom = (hasDock ? m.dockY - kDockGap : m.logY - 12);
    m.pageH = std::max(120, pageBottom - m.pageY);
    m.pageW = std::max(320, m.cardW - kScrollW - 2);
    m.scrollX = m.pageX + m.pageW + 2;
    m.scrollY = m.pageY;
    m.scrollW = kScrollW - 2;
    m.scrollH = m.pageH;
    return m;
}

// ---------------------------------------------------------------- control factories
static HBRUSH BgBrushFor(COLORREF c) {
    if (c == ColPage()) return g_brushPage;
    if (c == ColCard()) return g_brushCard;
    if (c == ColLog()) return g_brushLog;
    if (c == ColField()) return g_brushField;
    return g_brushBg;
}

static HWND ParentForPage(int page) {
    if (page >= 0 && page < g_optPageCount && g_hPages[page]) return g_hPages[page];
    return g_hMain;
}

static HWND MakeStatic(int id, int page, const wchar_t* text, int font, DWORD extra) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra,
                             0, 0, 10, 10, ParentForPage(page), (HMENU)(INT_PTR)id, g_hInst, nullptr);
    AddWidget(h, id, page, font);
    return h;
}

static HWND MakeButton(int id, int page, const wchar_t* text, int font, bool primary, bool danger,
                       bool nav, bool check, COLORREF bg) {
    DWORD style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    if (!nav) style |= WS_TABSTOP;
    HWND h = CreateWindowExW(0, L"BUTTON", text, style,
                             0, 0, 10, 10, ParentForPage(page), (HMENU)(INT_PTR)id, g_hInst, nullptr);
    BtnInfo* bi = new BtnInfo();
    bi->primary = primary;
    bi->nav = nav;
    bi->danger = danger;
    bi->check = check;
    bi->bg = bg;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)bi);
    SetWindowSubclass(h, BtnSubclass, 1, (DWORD_PTR)bi);
    AddWidget(h, id, page, font);
    return h;
}

static HWND MakeEdit(int id, int page, const wchar_t* text, DWORD extra, bool multiline) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT;
    style |= multiline ? (ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL)
                       : (ES_AUTOHSCROLL | extra);
    HWND h = CreateWindowExW(0, L"EDIT", text, style,
                             0, 0, 10, 10, ParentForPage(page), (HMENU)(INT_PTR)id, g_hInst, nullptr);
    if (!multiline) {
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(Dpi(9), Dpi(9)));
    } else {
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(Dpi(9), Dpi(9)));
    }
    SetWindowSubclass(h, EditBorderProc, 1, 0);
    AddWidget(h, id, page, multiline ? FONT_MONO : FONT_BODY);
    return h;
}

static HWND MakeList(int id, int page, const wchar_t* const* heads, int n) {
    HWND h = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS | WS_CLIPSIBLINGS,
                             0, 0, 10, 10, ParentForPage(page), (HMENU)(INT_PTR)id, g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
    AddColumns(h, heads, n);
    FluentStyleControl(h, L"DarkMode_Explorer");
    // Themed scrollbars are dark as well, and the list still gets a grey
    // surface from the very first paint (empty or not) because its background
    // is painted in WM_ERASEBKGND and in the custom draw prepaint.
    ListView_SetBkColor(h, ColPage());
    ListView_SetTextBkColor(h, ColPage());
    ListView_SetTextColor(h, ColText());
    SetWindowSubclass(h, DarkListSubclass, 1, 0);
    HWND hdr = (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0);
    if (hdr) {
        // No visual style: a themed header paints its own hover/pressed
        // column with system (light) colours on top of our dark bar.
        SetWindowTheme(hdr, L"", L"");
        SendMessageW(hdr, WM_SETFONT, (WPARAM)g_fonts[FONT_BODY], TRUE);
        SetWindowSubclass(hdr, HeaderPaintSubclass, 1, 0);
    }
    AddWidget(h, id, page, FONT_BODY);
    return h;
}

static void AddColumns(HWND lv, const wchar_t* const* heads, int n) {
    for (int i = 0; i < n; i++) {
        LVCOLUMNW c = { 0 };
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = (LPWSTR)heads[i];
        c.cx = Dpi(80);
        c.iSubItem = i;
        SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
    }
}

// ---------------------------------------------------------------- build
static void CreatePages() {
    for (int p = 0; p < g_optPageCount; p++) {
        // No WS_EX_COMPOSITED: a composited page rebuilds a redirection surface
        // on every resize step and every slide frame, which both costs
        // milliseconds and occasionally paints the surface black before the
        // children have caught up. The page paints its own background and each
        // control paints itself, so the surface is already flicker-free.
        g_hPages[p] = CreateWindowExW(0, kPageClass, L"",
                                      WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 10, 10, g_hMain, (HMENU)(INT_PTR)(0x4000 + p), g_hInst, nullptr);
    }
}

static void CreateTooltip() {
    g_hTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                             WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             g_hMain, nullptr, g_hInst, nullptr);
    if (!g_hTip) return;
    SendMessageW(g_hTip, TTM_SETMAXTIPWIDTH, 0, Dpi(420));
    SendMessageW(g_hTip, TTM_SETTIPBKCOLOR, 0, (LPARAM)ColCard());
    SendMessageW(g_hTip, TTM_SETTIPTEXTCOLOR, 0, (LPARAM)ColText());
    FluentStyleControl(g_hTip, L"DarkMode_Explorer");
    SendMessageW(g_hTip, WM_SETFONT, (WPARAM)g_fonts[FONT_SMALL], TRUE);
}

static void BuildShell() {
    HICON icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   Dpi(32), Dpi(32), LR_DEFAULTCOLOR);
    HWND ic = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                              0, 0, 10, 10, g_hMain, (HMENU)(INT_PTR)IDC_APPICON, g_hInst, nullptr);
    if (icon) SendMessageW(ic, STM_SETICON, (WPARAM)icon, 0);
    AddWidget(ic, IDC_APPICON, -1, FONT_BODY);

    MakeStatic(IDC_APPTITLE, -1, L"yt-dlp GUI", FONT_TITLE);

    HWND url = MakeEdit(IDC_URL, -1, L"");
    SendMessageW(url, EM_SETCUEBANNER, TRUE, (LPARAM)L"Paste url");

    MakeButton(IDC_PASTE, -1, L"Paste", FONT_BODY, false, false, false, false, ColCard());
    MakeButton(IDC_CLEARURL, -1, L"Clear", FONT_BODY, false, false, false, false, ColCard());
    MakeButton(IDC_CANCEL, -1, L"Cancel", FONT_BODY, false, true, false, false, ColCard());

    HWND prog = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                0, 0, 10, 10, g_hMain, (HMENU)(INT_PTR)IDC_PROGRESS, g_hInst, nullptr);
    SetWindowTheme(prog, L"", L"");
    SendMessageW(prog, PBM_SETBKCOLOR, 0, (LPARAM)ColField());
    SendMessageW(prog, PBM_SETBARCOLOR, 0, (LPARAM)ColAccent());
    SendMessageW(prog, PBM_SETRANGE32, 0, 1000);
    AddWidget(prog, IDC_PROGRESS, -1, FONT_SMALL);

    MakeStatic(IDC_STATUS, -1, L"Ready", FONT_SMALL);
    MakeButton(IDC_OPENFOLDER, -1, L"Open folder", FONT_BODY, false, false, false, false, ColCard());
    TipAdd(Ctl(IDC_OPENFOLDER), L"Open the current section's output folder in File Explorer.");
    MakeButton(IDC_SHOWCMD, -1, L"Show command", FONT_BODY, false, false, false, false, ColCard());
    MakeButton(IDC_CLEARLOG, -1, L"Clear log", FONT_BODY, false, false, false, false, ColCard());
    MakeEdit(IDC_LOG, -1, L"", 0, true);

    // One download action per section, docked in the bottom-right corner of the
    // row above the log.
    MakeButton(IDC_DOWNLOAD, -1, L"Download", FONT_BODY, true, false, false, false, ColBg());
    TipAdd(Ctl(IDC_DOWNLOAD), L"Fetch (when needed) and download the current section's link.");

    // Playlist-only companion to Download: reads the playlist and lists its
    // entries so they can be checked before downloading.
    MakeButton(IDC_PL_FETCH, -1, L"Fetch playlist", FONT_BODY, false, false, false, false, ColBg());
    TipAdd(Ctl(IDC_PL_FETCH), L"Read the playlist and list its videos so you can pick what to download.");
    ShowWindow(Ctl(IDC_PL_FETCH), SW_HIDE);

    MakeStatic(IDC_TOOLS, -1, L"", FONT_SMALL);

    MakeButton(IDC_INSTALL_TOOLS, -1, L"Install required tools", FONT_BODY, true, false, false, false, ColBg());
    TipAdd(Ctl(IDC_INSTALL_TOOLS), L"Opens an elevated PowerShell window, asks for Windows approval (UAC) and installs the missing tools with winget. Press Enter when it finishes; the app restarts.");
    ShowWindow(Ctl(IDC_INSTALL_TOOLS), SW_HIDE);
}

void BuildUi(HWND main) {
    g_hMain = main;
    g_brushBg = CreateSolidBrush(ColBg());
    g_brushPage = CreateSolidBrush(ColPage());
    g_brushCard = CreateSolidBrush(ColCard());
    g_brushField = CreateSolidBrush(ColField());
    g_brushLog = CreateSolidBrush(ColLog());
    g_brushBorder = CreateSolidBrush(ColBorder());

    RegisterComboClasses();
    RegisterPageClasses();
    CreatePages();
    CreateTooltip();

    g_hNav[0] = MakeButton(IDC_NAV_DOWNLOAD, -1, L"Video", FONT_BODY, false, false, true, false, ColBg());
    g_hNav[1] = MakeButton(IDC_NAV_PLAYLIST, -1, L"Playlist", FONT_BODY, false, false, true, false, ColBg());
    g_hNav[2] = MakeButton(IDC_NAV_AUDIO, -1, L"Audio", FONT_BODY, false, false, true, false, ColBg());
    g_hNav[3] = MakeButton(IDC_NAV_SETTINGS, -1, L"Settings", FONT_BODY, false, false, true, false, ColBg());

    BuildShell();
    BuildDownloadPage(0);
    BuildPlaylistPage(1);
    BuildAudioPage(3);
    for (int p = 1; p < g_optPageCount; p++) {
        if (p == 1 || p == 3) continue;  // custom pages handled above
        BuildOptPage(p);
    }

    g_hScroll = CreateWindowExW(0, kScrollClass, L"", WS_CHILD,
                                0, 0, 10, 10, main, (HMENU)(INT_PTR)IDC_SCROLL, g_hInst, nullptr);
    ShowWindow(g_hScroll, SW_HIDE);
    SetTab(0);
}

// ---------------------------------------------------------------- painting
static void PaintText(HDC dc, int x, int y, const wchar_t* s, int font, COLORREF color) {
    HFONT f = g_fonts[font] ? g_fonts[font] : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, s, (int)wcslen(s));
    SelectObject(dc, of);
}

void PaintMain(HDC dc, int w, int h) {
    if (w < 2 || h < 2) return;
    int lw = UnDpi(w), lh = UnDpi(h);
    int cardMode = CardModeForPage(g_page);
    UiMetrics m = ComputeMetrics(lw, lh, cardMode);

    // The off-screen buffer is reused between paints. Creating a full-window
    // bitmap on every WM_PAINT is what made fast resizes feel like they were
    // chasing the frame.
    static HDC s_mem = nullptr;
    static HBITMAP s_bmp = nullptr;
    static int s_w = 0, s_h = 0;
    if (!s_mem) s_mem = CreateCompatibleDC(dc);
    if (!s_bmp || w != s_w || h != s_h) {
        if (s_bmp) DeleteObject(s_bmp);
        s_bmp = CreateCompatibleBitmap(dc, w, h);
        s_w = w;
        s_h = h;
    }
    HGDIOBJ oldBmp = SelectObject(s_mem, s_bmp);

    RECT full = { 0, 0, w, h };
    // WM_PAINT may only cover a strip (the area a sliding page uncovered), so
    // restrict both the drawing and the final blit to that strip. Without this
    // every animation frame cost a full-window repaint.
    RECT clip = full;
    int boxType = GetClipBox(dc, &clip);
    if (boxType == ERROR || boxType == NULLREGION) {
        SelectObject(s_mem, oldBmp);
        return;
    }
    if (clip.left < 0) clip.left = 0;
    if (clip.top < 0) clip.top = 0;
    if (clip.right > w) clip.right = w;
    if (clip.bottom > h) clip.bottom = h;
    if (clip.right <= clip.left || clip.bottom <= clip.top) {
        SelectObject(s_mem, oldBmp);
        return;
    }
    SaveDC(s_mem);
    IntersectClipRect(s_mem, clip.left, clip.top, clip.right, clip.bottom);

    FillRect(s_mem, &full, g_brushBg);

    // sidebar separator
    {
        HPEN pen = CreatePen(PS_SOLID, 1, Mix(ColBg(), ColBorder(), 55));
        HGDIOBJ op = SelectObject(s_mem, pen);
        MoveToEx(s_mem, Dpi(m.sbW) - 1, 0, nullptr);
        LineTo(s_mem, Dpi(m.sbW) - 1, h);
        SelectObject(s_mem, op);
        DeleteObject(pen);
    }

    PaintText(s_mem, Dpi(56), Dpi(46), L"portable downloader", FONT_SMALL, ColMuted());
    PaintText(s_mem, Dpi(18), Dpi(76), L"NAVIGATION", FONT_SMALL, Mix(ColMuted(), ColBg(), 35));

    // link card (Video / Playlist / Audio)
    if (cardMode != CARD_NONE) {
        RECT card = { Dpi(m.cardX), Dpi(m.cardY), Dpi(m.cardX + m.cardW), Dpi(m.cardY + m.cardH) };
        FillRound(s_mem, card.left, card.top, card.right, card.bottom, Dpi(10), ColCard(), ColBorder());
    }

    // page card
    RECT page = { Dpi(m.pageX), Dpi(m.pageY), Dpi(m.pageX + m.pageW), Dpi(m.pageY + m.pageH) };
    FillRound(s_mem, page.left, page.top, page.right, page.bottom, Dpi(10), ColPage(), ColBorder());

    // log card
    RECT log = { Dpi(m.logX), Dpi(m.logY), Dpi(m.logX + m.logW), Dpi(m.logY + m.logH) };
    FillRound(s_mem, log.left, log.top, log.right, log.bottom, Dpi(10), ColCard(), ColBorder());

    // sidebar tool separator
    {
        int y = Dpi(lh - 88);
        HPEN pen = CreatePen(PS_SOLID, 1, Mix(ColBg(), ColBorder(), 55));
        HGDIOBJ op = SelectObject(s_mem, pen);
        MoveToEx(s_mem, Dpi(16), y, nullptr);
        LineTo(s_mem, Dpi(m.sbW - 16), y);
        SelectObject(s_mem, op);
        DeleteObject(pen);
    }
    PaintText(s_mem, Dpi(16), Dpi(lh - 80), L"TOOLS", FONT_SMALL, Mix(ColMuted(), ColBg(), 35));

    BitBlt(dc, clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top,
           s_mem, clip.left, clip.top, SRCCOPY);
    RestoreDC(s_mem, -1);
    SelectObject(s_mem, oldBmp);
}

// ---------------------------------------------------------------- layout
// Geometry that was actually applied to each page window. Re-applying an
// unchanged rect (and rebuilding its round region) is one of the most
// expensive things that can happen during a resize, so it is skipped.
static RECT g_pageApplied[8];
static bool g_pageAppliedValid[8] = { false };
static bool g_pageHasRegion[8] = { false };

static void ApplyPageRegion(int p, int pw, int ph) {
    HRGN rgn = CreateRoundRectRgn(Dpi(1), Dpi(1), pw - Dpi(1), ph - Dpi(1), Dpi(12), Dpi(12));
    SetWindowRgn(g_hPages[p], rgn, FALSE);   // no synchronous repaint
    g_pageHasRegion[p] = true;
}

static void MainLayoutImpl(int w, int h) {
    if (w < 2 || h < 2) return;
    CancelSmoothScroll();
    int lw = UnDpi(w), lh = UnDpi(h);
    int cardMode = CardModeForPage(g_page);
    UiMetrics m = ComputeMetrics(lw, lh, cardMode);
    CloseComboPopupIfAny();
    PlaceBegin();

    // sidebar
    Place(IDC_APPICON, 18, 18, 32, 32);
    Place(IDC_APPTITLE, 56, 14, m.sbW - 68, 32);
    int ny = kNavTop;
    Place(IDC_NAV_DOWNLOAD, 12, ny, m.sbW - 24, kNavH);
    ny += kNavH + kNavGap;
    Place(IDC_NAV_PLAYLIST, 12, ny, m.sbW - 24, kNavH);
    ny += kNavH + kNavGap;
    Place(IDC_NAV_AUDIO, 12, ny, m.sbW - 24, kNavH);
    ny += kNavH + kNavGap;
    Place(IDC_NAV_SETTINGS, 12, ny, m.sbW - 24, kNavH);
    Place(IDC_INSTALL_TOOLS, 12, lh - 134, m.sbW - 24, 34);
    Place(IDC_TOOLS, 16, lh - 78, m.sbW - 32, 64);

    bool toolsMissing = g_tools.ytdlp.empty() || g_tools.ffmpeg.empty() || g_tools.deno.empty();
    ShowCtl(IDC_INSTALL_TOOLS, toolsMissing);

    // link row (no label above the field, the cue banner is the label)
    bool cardPage = cardMode != CARD_NONE;
    ShowCtl(IDC_URL, cardPage);
    ShowCtl(IDC_PASTE, cardPage);
    ShowCtl(IDC_CLEARURL, cardPage);
    ShowCtl(IDC_DOWNLOAD, cardPage);
    ShowCtl(IDC_PL_FETCH, g_page == 1);
    if (cardPage) {
        int y1 = m.cardY + 12;
        int bPaste = 68, bClear = 64;
        int urlW = m.cardW - kCardPad * 2 - (bPaste + bClear) - 16 - 10;
        if (urlW < 180) urlW = 180;
        Place(IDC_URL, m.cardX + kCardPad, y1, urlW, 32);
        int bx = m.cardX + kCardPad + urlW + 10;
        Place(IDC_PASTE, bx, y1, bPaste, 32); bx += bPaste + 8;
        Place(IDC_CLEARURL, bx, y1, bClear, 32);
    }

    // The download row sits directly above the log card and is pinned to its
    // right edge. Playlist adds a Fetch action to the left of Download.
    Place(IDC_DOWNLOAD, m.dockX, m.dockY, m.dockW, m.dockH);
    Place(IDC_PL_FETCH, m.dockX - kFetchW - 8, m.dockY, kFetchW, m.dockH);

    // log card
    int lx = m.logX + kCardPad;
    int lw2 = m.logW - kCardPad * 2;
    int bOpen = 112, bShow = 122, bClr = 96, bCancel = 90;
    Place(IDC_STATUS, lx, m.logY + 9, lw2 - (bOpen + bShow + bClr + bCancel) - 32, 18);
    int lbtn = m.logX + m.logW - kCardPad - bClr;
    Place(IDC_CLEARLOG, lbtn, m.logY + 6, bClr, 28); lbtn -= bShow + 8;
    Place(IDC_SHOWCMD, lbtn, m.logY + 6, bShow, 28); lbtn -= bOpen + 8;
    Place(IDC_OPENFOLDER, lbtn, m.logY + 6, bOpen, 28); lbtn -= bCancel + 8;
    Place(IDC_CANCEL, lbtn, m.logY + 6, bCancel, 28);
    Place(IDC_PROGRESS, lx, m.logY + 36, lw2, 7);
    Place(IDC_LOG, lx, m.logY + 50, lw2, std::max(40, m.logH - 62));

    PlaceEnd();

    // Page windows: only the visible one is moved. Hidden pages are positioned
    // lazily by SetTab when they are about to be shown, which removes seven
    // pointless SetWindowPos calls from every resize step.
    int pw = Dpi(m.pageW), ph = Dpi(m.pageH);
    HWND page = g_hPages[g_page];
    if (page) {
        RECT want = { Dpi(m.pageX), Dpi(m.pageY), Dpi(m.pageX + m.pageW), Dpi(m.pageY + m.pageH) };
        bool same = g_pageAppliedValid[g_page] && EqualRect(&want, &g_pageApplied[g_page]);
        if (!same) {
            SetWindowPos(page, nullptr, want.left, want.top, want.right - want.left,
                         want.bottom - want.top, SWP_NOZORDER | SWP_NOACTIVATE);
            g_pageApplied[g_page] = want;
            g_pageAppliedValid[g_page] = true;
            g_pageHasRegion[g_page] = false;
        }
        if (!g_pageHasRegion[g_page]) ApplyPageRegion(g_page, pw, ph);
        if (!same) InvalidateRect(page, nullptr, TRUE);   // no stale strips
    }

    static RECT s_scrollRect = { 0 };
    static bool s_scrollRectValid = false;
    RECT scrollWant = { Dpi(m.scrollX), Dpi(m.scrollY), Dpi(m.scrollX + m.scrollW), Dpi(m.scrollY + m.scrollH) };
    if (!s_scrollRectValid || !EqualRect(&scrollWant, &s_scrollRect)) {
        s_scrollRect = scrollWant;
        s_scrollRectValid = true;
        SetWindowPos(g_hScroll, nullptr, scrollWant.left, scrollWant.top,
                     scrollWant.right - scrollWant.left, scrollWant.bottom - scrollWant.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Only the visible page needs a fresh layout; hidden pages are laid out
    // when they are shown. This keeps page switches and window resizes fast.
    HWND cur = g_hPages[g_page];
    if (cur) {
        RECT pr;
        GetClientRect(cur, &pr);
        int ph2 = UnDpi(pr.bottom);
        int maxOff = g_contentH[g_page] - ph2;
        if (maxOff < 0) maxOff = 0;
        if (g_scrollY[g_page] > maxOff) g_scrollY[g_page] = maxOff;
        LayoutOnePage(g_page, UnDpi(pr.right), ph2);
    }
    UpdateScrollbar();
    RefreshDownloadUi();
    InvalidateRect(g_hMain, nullptr, FALSE);
}

void MainLayout(int w, int h) {
    MainLayoutImpl(w, h);
}

HBRUSH BrushForWindow(HWND h) {
    if (!h) return g_brushBg;
    if (h == Ctl(IDC_LOG)) return g_brushLog;
    int id = GetDlgCtrlID(h);
    if (id == IDC_LBL_URL || id == IDC_STATUS) return g_brushCard;
    if (PageAtPoint(h) >= 0) return g_brushPage;
    return g_brushBg;
}

// ---------------------------------------------------------------- navigation
void RefreshDownloadUi() {
    // One download button serves all three card sections; it is always visible
    // there. Playlist adds its own fetch action next to it.
    bool cardPage = CardModeForPage(g_page) != CARD_NONE;
    ShowCtl(IDC_DOWNLOAD, cardPage);
    ShowCtl(IDC_PL_FETCH, g_page == 1);
    if (cardPage) {
        const wchar_t* label = g_page == 1 ? L"Download playlist"
                              : g_page == 3 ? L"Download audio"
                                            : L"Download video";
        if (g_page == 1 && Ctl(IDC_PL_AUDIO) && CtlChecked(IDC_PL_AUDIO))
            label = L"Download playlist (MP3)";
        SetCtlText(IDC_DOWNLOAD, label);
    }
}

void SetTab(int page) {
    if (page < 0 || page >= g_optPageCount) return;
    RECT rc;
    if (g_hMain) GetClientRect(g_hMain, &rc);
    else rc = { 0, 0, Dpi(1140), Dpi(830) };

    CancelSmoothScroll();
    CloseComboPopupIfAny();

    for (int p = 0; p < g_optPageCount; p++)
        if (g_hNav[p]) InvalidateRect(g_hNav[p], nullptr, FALSE);

    g_page = page;
    MainLayout(rc.right, rc.bottom);
    ForceColumnLayout();

    // Switching is instant. The incoming page is laid out and painted
    // synchronously while the outgoing page is still on screen; only once the
    // new page has every pixel does the old one get hidden. Nothing is ever
    // uncovered in between, so the switch reads as the same card changing
    // content instead of a transition.
    HWND cur = g_hPages[page];
    if (cur) {
        SetWindowPos(cur, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(cur, SW_SHOW);
        // One synchronous paint covers the page and every child (lists included,
        // which otherwise keep whatever they were drawn as at 10x10).
        RedrawWindow(cur, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    for (int p = 0; p < g_optPageCount; p++) {
        if (p == page || !g_hPages[p]) continue;
        ShowWindow(g_hPages[p], SW_HIDE);
    }

    UpdateScrollbar();
    InvalidateRect(g_hMain, nullptr, FALSE);
    // Paint the chrome (navigation highlight, the strips the outgoing card left
    // behind) in the same message so the old and new sections never coexist on
    // screen for a frame.
    UpdateWindow(g_hMain);
}

// ---------------------------------------------------------------- option page builder
static void BuildOptPage(int p) {
    const OptPageDef& pg = g_optPages[p];
    for (int i = 0; i < pg.count; i++) {
        const OptDef& d = pg.defs[i];
        int id = PageBase(p) + i;
        switch (d.kind) {
            case OPT_HDR:
                MakeStatic(id, p, d.label, FONT_BOLD);
                break;
            case OPT_CHECK:
                MakeButton(id, p, d.label, FONT_BODY, false, false, false, true, ColPage());
                if (d.def && wcscmp(d.def, L"1") == 0)
                    SendMessageW(Ctl(id), BM_SETCHECK, BST_CHECKED, 0);
                break;
            case OPT_EDIT: {
                MakeStatic(id + 500, p, d.label, FONT_SMALL);
                DWORD extra = 0;
                if (d.flag && (wcscmp(d.flag, L"-p") == 0 || wcscmp(d.flag, L"--video-password") == 0))
                    extra = ES_PASSWORD;
                MakeEdit(id, p, (d.def && d.def[0]) ? d.def : L"", extra);
                break;
            }
            case OPT_FILE: {
                MakeStatic(id + 500, p, d.label, FONT_SMALL);
                MakeEdit(id, p, (d.def && d.def[0]) ? d.def : L"");
                MakeButton(id + 600, p, L"...", FONT_BODY, false, false, false, false, ColPage());
                break;
            }
            case OPT_COMBO: {
                MakeStatic(id + 500, p, d.label, FONT_SMALL);
                HWND cb = MakeCombo(id, p);
                ComboFill(cb, d.items, true);
                if (d.def && d.def[0]) {
                    int cnt = (int)SendMessageW(cb, CB_GETCOUNT, 0, 0);
                    for (int k = 0; k < cnt; k++) {
                        wchar_t buf[512] = { 0 };
                        SendMessageW(cb, CB_GETLBTEXT, (WPARAM)k, (LPARAM)buf);
                        if (wcscmp(buf, d.def) == 0) { SendMessageW(cb, CB_SETCURSEL, (WPARAM)k, 0); break; }
                    }
                }
                break;
            }
            case OPT_BTN:
                MakeButton(id, p, d.label, FONT_BODY, false, false, false, false, ColPage());
                break;
        }
        if (d.tip && d.tip[0]) {
            HWND ctl = Ctl(id);
            if (ctl) TipAdd(ctl, d.tip);
        }
    }

    // Apply / Reset footer only on the Settings page.
    if (p == g_optPageCount - 1) {
        MakeStatic(IDC_SET_HINT, p,
                   L"Apply writes every setting on this page to yt-dlp-gui-settings.txt in the app folder. "
                   L"Reset deletes that file and restores the defaults.",
                   FONT_SMALL);
        MakeButton(IDC_SET_APPLY, p, L"Apply settings", FONT_BODY, true, false, false, false, ColPage());
        MakeButton(IDC_SET_RESET, p, L"Reset to defaults", FONT_BODY, false, false, false, false, ColPage());
        TipAdd(Ctl(IDC_SET_APPLY), L"Save the current settings to yt-dlp-gui-settings.txt.");
        TipAdd(Ctl(IDC_SET_RESET), L"Delete the settings file and restore every default value.");
    }
}

// ---------------------------------------------------------------- page layout
static PageMetrics Measure(int p, int pw) {
    PageMetrics m;
    m.pad = 20;
    m.gap = 24;
    int avail = pw - m.pad * 2;
    const OptPageDef& pg = g_optPages[p];
    int n = pg.count;
    if (pw < 780) {
        m.gap = 0;
        m.colW = std::max(260, avail);
        m.split = n;
        return m;
    }
    avail -= m.gap;
    if (avail < 480) avail = 480;
    m.colW = avail / 2;
    int total = 0;
    std::vector<int> hts((size_t)n);
    for (int i = 0; i < n; i++) {
        int k = pg.defs[i].kind;
        hts[(size_t)i] = (k == OPT_HDR) ? 34 : (k == OPT_CHECK ? 30 : 36);
        total += hts[(size_t)i];
    }
    int split = n, best = 1 << 30, cum = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && pg.defs[i].kind == OPT_HDR) {
            int diff = cum * 2 - total;
            if (diff < 0) diff = -diff;
            if (diff < best) { best = diff; split = i; }
        }
        cum += hts[(size_t)i];
    }
    if (split >= n) {
        cum = 0;
        best = 1 << 30;
        for (int i = 0; i <= n; i++) {
            int diff = cum * 2 - total;
            if (diff < 0) diff = -diff;
            if (diff < best) { best = diff; split = i; }
            if (i < n) cum += hts[(size_t)i];
        }
    }
    if (split <= 0) split = n;
    m.split = split;
    return m;
}

static void LayoutDownloadPage(int p, int pw, int ph) {
    int pad = 20, gap = 12;
    int cw = pw - pad * 2;
    if (cw < 400) cw = 400;
    int y = 14 - g_scrollY[p];

    PlaceBegin();

    // Quality / Container
    Place(IDC_LBL_QUALITY, pad, y, cw / 2, 16);
    Place(IDC_LBL_CONT, pad + cw / 2 + gap, y, cw / 2, 16);
    y += 19;

    int colW = (cw - gap) / 2;
    Place(IDC_QUALITY, pad, y, colW, 32);
    Place(IDC_CONTAINER, pad + colW + gap, y, colW, 32);
    y += 41;

    int browseW = 98;
    Place(IDC_LBL_OUTDIR, pad, y, cw, 16);
    Place(IDC_OUTDIR, pad, y + 18, cw - browseW - gap, 32);
    Place(IDC_BROWSEOUT, pad + cw - browseW, y + 18, browseW, 32);
    y += 56;

    PlaceEnd();

    g_contentH[p] = y + g_scrollY[p] + pad - 14;
    (void)ph;
}

// ---------------------------------------------------------------- playlist page layout
// Width a checkbox-style control needs for its label at the current DPI, in
// logical pixels (the same units Place() takes).
static int ToggleWidth(const wchar_t* text) {
    int w = 120;
    HDC dc = GetDC(g_hMain);
    if (dc) {
        HGDIOBJ old = SelectObject(dc, g_fonts[FONT_BODY]);
        SIZE sz = { 0, 0 };
        if (GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz) && sz.cx > 0) w = UnDpi(sz.cx);
        SelectObject(dc, old);
        ReleaseDC(g_hMain, dc);
    }
    return w + 34;   // checkbox square + gap + a little breathing room
}

static void LayoutPlaylistPage(int p, int pw, int ph) {
    int pad = 20, gap = 12;
    int cw = pw - pad * 2;
    if (cw < 400) cw = 400;
    int y = 14 - g_scrollY[p];

    PlaceBegin();

    // Quality / Container for playlist downloads
    Place(IDC_PL_LBL_QUALITY, pad, y, cw / 2, 16);
    Place(IDC_PL_LBL_CONT, pad + cw / 2 + gap, y, cw / 2, 16);
    y += 19;

    int colW = (cw - gap) / 2;
    Place(IDC_PL_QUALITY, pad, y, colW, 32);
    Place(IDC_PL_CONTAINER, pad + colW + gap, y, colW, 32);
    y += 41;

    // Selection row and the per-playlist options. "Audio only" sits directly
    // beside "Create folder per playlist"; both are toggles, not actions.
    int availRow = cw - gap * 3;
    int wFolder = ToggleWidth(L"Create folder per playlist");
    int wAudio = ToggleWidth(L"Audio only (MP3)");
    int wSel = (availRow - wFolder - wAudio) / 2;
    if (wSel < 90) wSel = 90;
    Place(IDC_PL_SELECT_ALL, pad, y, wSel, 28);
    Place(IDC_PL_SELECT_NONE, pad + wSel + gap, y, wSel, 28);
    Place(IDC_PL_FOLDER, pad + (wSel + gap) * 2, y, wFolder, 28);
    Place(IDC_PL_AUDIO, pad + (wSel + gap) * 2 + wFolder + gap, y, wAudio, 28);
    y += 36;

    // Output folder stays above the list, so no scrolling is needed to reach it.
    int browseW = 98;
    Place(IDC_PL_LBL_OUTDIR, pad, y, cw, 16);
    Place(IDC_PL_OUTDIR, pad, y + 18, cw - browseW - gap, 32);
    Place(IDC_PL_BROWSE, pad + cw - browseW, y + 18, browseW, 32);
    y += 56;

    // List fills the remaining page height (never pushes the output row out of view)
    int baseY = y + g_scrollY[p];
    int listH = ph - baseY - pad;
    if (listH < 170) listH = 170;
    Place(IDC_PL_LIST, pad, y, cw, listH);
    static const int plPct[] = { 7, 43, 12, 15, 23 };
    SetColPercents(Ctl(IDC_PL_LIST), plPct, 5, cw);
    y += listH + pad;

    PlaceEnd();

    g_contentH[p] = y + g_scrollY[p];
    (void)ph;
}

// ---------------------------------------------------------------- audio page layout
static void LayoutAudioPage(int p, int pw, int ph) {
    int pad = 20, gap = 12;
    int cw = pw - pad * 2;
    if (cw < 400) cw = 400;
    int y = 14 - g_scrollY[p];

    PlaceBegin();

    int browseW = 98;
    Place(IDC_AUD_LBL_OUTDIR, pad, y, cw, 16);
    Place(IDC_AUD_OUTDIR, pad, y + 17, cw - browseW - gap, 32);
    Place(IDC_AUD_BROWSE, pad + cw - browseW, y + 17, browseW, 32);
    y += 49;

    PlaceEnd();

    g_contentH[p] = y + g_scrollY[p] + pad - 14;
    (void)ph;
}

// ---------------------------------------------------------------- option pages layout
static void LayoutOptPage(int p, int pw, int ph) {
    const OptPageDef& pg = g_optPages[p];
    int n = pg.count;
    PageMetrics m = Measure(p, pw);
    int xs[2] = { m.pad, m.pad + m.colW + m.gap };
    int y[2] = { 12 - g_scrollY[p], 12 - g_scrollY[p] };
    int labelW = ClampI(m.colW * 44 / 100, 118, 178);

    PlaceBegin();

    for (int i = 0; i < n; i++) {
        const OptDef& d = pg.defs[i];
        int id = PageBase(p) + i;
        int c = (i < m.split) ? 0 : 1;
        int x = xs[c];
        int cw = m.colW;
        switch (d.kind) {
            case OPT_HDR:
                if (y[c] > 12 - g_scrollY[p]) y[c] += 8;
                Place(id, x, y[c] + 2, cw, 22);
                y[c] += 32;
                break;
            case OPT_CHECK:
                Place(id, x, y[c], cw, 25);
                y[c] += 30;
                break;
            case OPT_EDIT:
            case OPT_FILE: {
                int btnW = (d.kind == OPT_FILE) ? 38 : 0;
                Place(id + 500, x, y[c] + 2, labelW, 17);
                Place(id, x + labelW + 6, y[c], cw - labelW - 6 - btnW, 30);
                if (d.kind == OPT_FILE)
                    Place(id + 600, x + cw - btnW, y[c], btnW, 30);
                y[c] += 36;
                break;
            }
            case OPT_COMBO:
                Place(id + 500, x, y[c] + 2, labelW, 17);
                Place(id, x + labelW + 6, y[c], cw - labelW - 6, 30);
                y[c] += 36;
                break;
            case OPT_BTN:
                Place(id, x, y[c], cw, 30);
                y[c] += 36;
                break;
        }
    }
    int bottom = (y[0] > y[1] ? y[0] : y[1]);
    if (p == g_optPageCount - 1) {
        int fy = bottom + 20;
        Place(IDC_SET_HINT, m.pad, fy, m.colW * 2 + m.gap, 18);
        fy += 26;
        Place(IDC_SET_APPLY, m.pad, fy, 150, 32);
        Place(IDC_SET_RESET, m.pad + 158, fy, 170, 32);
        bottom = fy + 32;
    }
    g_contentH[p] = bottom + g_scrollY[p] + m.pad - 8;
    (void)ph;

    PlaceEnd();
}

static void LayoutOnePage(int p, int pw, int ph) {
    if (p == 0)
        LayoutDownloadPage(p, pw, ph);
    else if (p == 1)
        LayoutPlaylistPage(p, pw, ph);
    else if (p == 3)
        LayoutAudioPage(p, pw, ph);
    else
        LayoutOptPage(p, pw, ph);
}

// Re-applies column widths exactly, even if the delta throttle skipped the
// last resize steps. Called when a drag ends and when a page is switched to.
void ForceColumnLayout() {
    HWND cur = g_hPages[g_page];
    if (!cur || !g_hMain) return;
    RECT pr;
    GetClientRect(cur, &pr);
    g_colForce = true;
    LayoutOnePage(g_page, UnDpi(pr.right), UnDpi(pr.bottom));
    g_colForce = false;
}

void LayoutPages() {
    for (int p = 0; p < g_optPageCount; p++) {
        HWND h = g_hPages[p];
        if (!h) continue;
        RECT rc;
        GetClientRect(h, &rc);
        int pw = UnDpi(rc.right);
        int ph = UnDpi(rc.bottom);
        LayoutOnePage(p, pw, ph);
        int maxOff = g_contentH[p] - ph;
        if (maxOff < 0) maxOff = 0;
        if (g_scrollY[p] > maxOff) {
            g_scrollY[p] = maxOff;
            LayoutOnePage(p, pw, ph);
        }
    }
    UpdateScrollbar();
}

static int MaxScrollFor(int page) {
    HWND h = g_hPages[page];
    if (!h) return 0;
    RECT rc;
    GetClientRect(h, &rc);
    int ph = UnDpi(rc.bottom);
    int maxOff = g_contentH[page] - ph;
    return maxOff < 0 ? 0 : maxOff;
}

static void ApplyScrollNow(int page, int off) {
    HWND h = g_hPages[page];
    if (!h) return;
    int maxOff = MaxScrollFor(page);
    if (off < 0) off = 0;
    if (off > maxOff) off = maxOff;
    int dy = g_scrollY[page] - off;   // logical units: content moves up by dy
    if (dy == 0) return;
    g_scrollY[page] = off;

    // Every page lays its controls out as a rigid vertical translation of
    // g_scrollY, so scrolling never needs a relayout. Relaying out ~40 controls
    // per animation tick (and asking for an asynchronous redraw of all of them)
    // is what made fast wheel scrolling tear: each control repainted on its own
    // schedule. Scrolling the page surface moves the pixels and the child
    // windows by the same delta in one step, then the uncovered strips are
    // painted immediately, so labels and fields stay glued together.
    ScrollWindowEx(h, 0, Dpi(dy), nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateScrollbar();
    UpdateWindow(h);
}

void ScrollPageTo(int page, int off) {
    if (page < 0 || page >= g_optPageCount) return;
    HWND h = g_hPages[page];
    if (!h) return;
    CancelSmoothScroll();
    CloseComboPopupIfAny();
    ApplyScrollNow(page, off);
    g_scrollTarget[page] = g_scrollY[page];
}

// Wheel input animates towards a target with a short 60 fps easing; the page
// repaints only the strips a moving control uncovers, so scrolling stays crisp.
void AnimateScrollBy(int page, int delta) {
    if (page < 0 || page >= g_optPageCount || page != g_page) return;
    int base = g_scrollTimerOn ? g_scrollTarget[page] : g_scrollY[page];
    int off = base + delta;
    int maxOff = MaxScrollFor(page);
    if (off < 0) off = 0;
    if (off > maxOff) off = maxOff;
    if (g_scrollTimerOn && off == g_scrollTarget[page]) return;
    if (!g_scrollTimerOn && off == g_scrollY[page]) return;
    g_scrollTarget[page] = off;
    if (!g_scrollTimerOn) {
        g_scrollTimerOn = true;
        SetTimer(g_hMain, IDT_SMOOTH_SCROLL, 10, nullptr);
    }
}

void SmoothScrollTick() {
    if (!g_scrollTimerOn) return;
    int page = g_page;
    if (page < 0 || page >= g_optPageCount) {
        KillTimer(g_hMain, IDT_SMOOTH_SCROLL);
        g_scrollTimerOn = false;
        return;
    }
    int cur = g_scrollY[page];
    int target = g_scrollTarget[page];
    if (cur == target) {
        KillTimer(g_hMain, IDT_SMOOTH_SCROLL);
        g_scrollTimerOn = false;
        return;
    }
    int diff = target - cur;
    int step = diff * 30 / 100;
    if (step == 0) step = diff > 0 ? 1 : -1;
    ApplyScrollNow(page, cur + step);
    if (g_scrollY[page] == target) {
        KillTimer(g_hMain, IDT_SMOOTH_SCROLL);
        g_scrollTimerOn = false;
    }
}

void CancelSmoothScroll() {
    if (g_scrollTimerOn) {
        KillTimer(g_hMain, IDT_SMOOTH_SCROLL);
        g_scrollTimerOn = false;
    }
    for (int i = 0; i < g_optPageCount; i++) g_scrollTarget[i] = g_scrollY[i];
}

void UpdateScrollbar() {
    if (!g_hScroll) return;
    static bool shown = false;
    HWND page = g_hPages[g_page];
    if (!page) {
        if (shown) { ShowWindow(g_hScroll, SW_HIDE); shown = false; }
        return;
    }
    RECT rc;
    GetClientRect(page, &rc);
    int ph = UnDpi(rc.bottom);
    bool need = g_contentH[g_page] > ph + 1;
    if (need != shown) {
        ShowWindow(g_hScroll, need ? SW_SHOW : SW_HIDE);
        shown = need;
    }
    if (need) InvalidateRect(g_hScroll, nullptr, FALSE);
}

// ---------------------------------------------------------------- tools sidebar
void UpdateToolsUi() {
    bool missing = g_tools.ytdlp.empty() || g_tools.ffmpeg.empty() || g_tools.deno.empty();
    HWND install = Ctl(IDC_INSTALL_TOOLS);
    if (install) ShowWindow(install, missing ? SW_SHOW : SW_HIDE);
    if (g_hMain) {
        RECT rc;
        GetClientRect(g_hMain, &rc);
        MainLayout(rc.right, rc.bottom);
    }
}

static void ComboItems(HWND h, const wchar_t* const* items, int n, int sel) {
    for (int i = 0; i < n; i++) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(h, CB_SETCURSEL, (WPARAM)sel, 0);
}
