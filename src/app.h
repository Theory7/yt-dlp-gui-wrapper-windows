// yt-dlp GUI - shared declarations
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

#define APP_NAME  L"yt-dlp GUI"
#define APP_VER   L"1.0.0"
#define APP_CLASS L"YtDlpGuiMain"
#define CFG_NAME  L"yt-dlp-gui-settings.txt"

// ---- internal messages
#define WM_APP_LOG      (WM_APP + 1)   // LPARAM: std::wstring*
#define WM_APP_PROGRESS (WM_APP + 2)   // LPARAM: ProgressInfo*
#define WM_APP_RUNDONE  (WM_APP + 3)   // WPARAM: mode, LPARAM: exit code
#define WM_APP_META     (WM_APP + 4)   // LPARAM: MetaResult*
#define WM_APP_TOOLS    (WM_APP + 6)   // tools were (un)installed, redetect
#define WM_APP_RESTART  (WM_APP + 5)   // close and reopen the app (new tools are visible)

#define IDT_SMOOTH_SCROLL 7001

// ---- control ids
enum : int {
    // ---- navigation rail (left sidebar)
    IDC_NAV_BASE = 60,
    IDC_NAV_DOWNLOAD = 60,
    IDC_NAV_PLAYLIST = 61,
    IDC_NAV_AUDIO = 62,
    IDC_NAV_SETTINGS = 63,
    IDC_SCROLL = 69,

    // ---- download section (main card)
    IDC_LBL_URL = 100, IDC_URL = 101,
    IDC_PASTE = 102, IDC_FETCH = 103, IDC_CLEARURL = 104,
    IDC_LBL_QUALITY = 105, IDC_QUALITY = 106,
    IDC_LBL_CONT = 109, IDC_CONTAINER = 110,
    IDC_DOWNLOAD = 111, IDC_DOWNLOAD_UNMERGED = 112, IDC_LISTFORMATS = 113,
    IDC_FMTCODE = 114, IDC_LBL_FMTCODE = 115,
    IDC_DL_HINT = 119,

    // ---- shared log toolbar (bottom, visible on all sections)
    IDC_PROGRESS = 200, IDC_STATUS = 201, IDC_LOG = 202,
    IDC_OPENFOLDER = 203, IDC_SHOWCMD = 204, IDC_CLEARLOG = 205,
    IDC_CANCEL = 206,

    // ---- app-wide keyboard command (Ctrl+Q closes the app)
    IDC_QUIT_APP = 90,

    // ---- playlist section (400-499)
    IDC_PL_FETCH = 400, IDC_PL_LIST = 402,
    IDC_PL_SELECT_ALL = 403, IDC_PL_SELECT_NONE = 404,
    IDC_PL_DOWNLOAD_ALL = 405, IDC_PL_DOWNLOAD_SEL = 406,
    IDC_PL_FOLDER = 407, IDC_PL_NUMBERS = 408, IDC_PL_SKIP = 409,
    IDC_PL_OUTDIR = 410, IDC_PL_BROWSE = 411,
    IDC_PL_LBL_OUTDIR = 412,
    IDC_PL_LBL_QUALITY = 413, IDC_PL_QUALITY = 414,
    IDC_PL_LBL_CONT = 417, IDC_PL_CONTAINER = 418,
    // Shared controls on playlist page (separate IDs to avoid duplicates)
    IDC_PL_LBL_URL = 419, IDC_PL_URL = 420,
    IDC_PL_PASTE = 421, IDC_PL_FETCH_BTN = 422, IDC_PL_CLEARURL = 423,
    IDC_PL_DOWNLOAD = 424, IDC_PL_DOWNLOAD_UNMERGED = 425,
    IDC_PL_SUBS = 426, IDC_PL_THUMB = 427,
    IDC_PL_AUDIO = 428,   // playlist toggle: save entries as MP3 audio only

    // ---- audio section (500-599)
    IDC_AUD_EXTRACT = 500, IDC_AUD_FORMAT = 501, IDC_AUD_QUALITY = 502,
    IDC_AUD_TPL = 505, IDC_AUD_OUTDIR = 506, IDC_AUD_BROWSE = 507,
    IDC_AUD_DOWNLOAD = 508,
    IDC_AUD_DL_TOP = 513,   // "Download" in the link card on the Audio page
    IDC_AUD_LBL_FORMAT = 509, IDC_AUD_LBL_QUALITY = 510,
    IDC_AUD_LBL_TPL = 511, IDC_AUD_LBL_OUTDIR = 512,

    // ---- chrome
    IDC_APPICON = 300, IDC_APPTITLE = 301, IDC_TOOLS = 302,
    IDC_INSTALL_TOOLS = 304,

    // ---- settings / tools actions (Settings page footer)
    IDC_SET_APPLY = 700, IDC_SET_RESET = 701, IDC_SET_HINT = 702,

    // ---- settings page options (option pages, 1000+)
    IDC_OPT_BASE = 1000,

    // legacy IDs still referenced by main.cpp / options.cpp (kept for compatibility)
    IDC_OUTDIR = 1001,
    IDC_OUTTMPL = 1002,
    IDC_FMTLIST = 1003,
    IDC_PLLIST = 1005,
    IDC_SINGLE = 1007,
    IDC_PLAYMODE = 1010,
    IDC_BROWSEOUT = 1011,
    IDC_SEC_FORMAT = 1012,
    IDC_SEC_STREAM = 1013,
    IDC_SEC_OUTPUT = 1014,
    IDC_SEC_PLAYLIST = 1015,
    IDC_LBL_FMTLIST = 1018,
    IDC_LBL_OUTDIR = 1019,
    IDC_LBL_TPL = 1020,
    IDC_LBL_PLAYLIST = 1021,
    IDC_PL_ALL = 1024,
    IDC_PL_SEL = 1025,
    IDC_PL_ITEMS = 1026
};
// list view context menu commands (kept below IDC_OPT_BASE on purpose)
enum : int { IDM_FMT_DOWNLOAD = 300, IDM_FMT_USE, IDM_FMT_COPY };

// ---- option page model
enum OptKind { OPT_CHECK = 0, OPT_EDIT, OPT_COMBO, OPT_FILE, OPT_BTN, OPT_HDR };

struct OptDef {
    int kind;
    const wchar_t* label;
    const wchar_t* flag;   // on-flag for checkboxes, value-flag for edit/combo/button
    const wchar_t* def;    // default value (edit) or preselected combo text
    const wchar_t* items;  // combo items, '|' separated
    const wchar_t* tip;    // tooltip / help
};

struct OptPageDef {
    const wchar_t* title;
    const OptDef* defs;
    int count;
    int custom = 0;  // 1 = download page, 2 = playlist page (hand laid out)
};

extern const OptPageDef g_optPages[];
extern const int g_optPageCount;
int PageBase(int page);
int OptIdByFlag(const wchar_t* flag);
const OptDef* DefByCtlId(int id, int* page, int* idx);
std::wstring FmtHeightFilter(int h);

// ---- widget registry (logical 96dpi coords are produced by layout code)
enum { FONT_BODY = 0, FONT_BOLD = 1, FONT_MONO = 2, FONT_SMALL = 3, FONT_TITLE = 4 };

struct Widget { HWND h; int id; int page; int font; };

// ---- Windows 11 dark palette (proper Fluent Design System dark mode)
inline COLORREF ColBg()        { return RGB(27, 27, 27); }       // #1B1B1B - Window background
inline COLORREF ColPage()      { return RGB(41, 41, 41); }       // #292929 - Page/surface background
inline COLORREF ColCard()      { return RGB(51, 51, 51); }       // #333333 - Card/elevated surface
inline COLORREF ColField()     { return RGB(51, 51, 51); }       // #333333 - Input field background
inline COLORREF ColFieldHot()  { return RGB(58, 58, 60); }       // #3A3A3C - Hovered field
inline COLORREF ColBorder()    { return RGB(63, 63, 70); }       // #3F3F46 - Border
inline COLORREF ColBorderHot() { return RGB(96, 96, 108); }      // #60606C - Hovered border
inline COLORREF ColText()      { return RGB(255, 255, 255); }    // #FFFFFF - Primary text
inline COLORREF ColMuted()     { return RGB(154, 154, 154); }    // #9A9A9A - Secondary text
inline COLORREF ColAccent()    { return RGB(0, 120, 212); }      // #0078D4 - Windows blue accent
inline COLORREF ColAccentDim() { return RGB(0, 90, 158); }       // #005A9E - Dim accent
inline COLORREF ColAccentText(){ return RGB(255, 255, 255); }    // #FFFFFF - Text on accent
inline COLORREF ColSel()       { return RGB(0, 96, 192); }       // #0060C0 - Selection
inline COLORREF ColLog()       { return RGB(27, 27, 27); }       // #1B1B1B - Log background
inline COLORREF ColDanger()    { return RGB(232, 17, 35); }      // #E81123 - Danger red
inline COLORREF ColHover()     { return RGB(58, 58, 60); }       // #3A3A3C - Hover background
inline COLORREF ColPressed()   { return RGB(45, 45, 45); }       // #2D2D2D - Pressed background

extern HINSTANCE g_hInst;
extern HWND g_hMain, g_hTip;
extern HWND g_hPages[8];
extern HWND g_hNav[8];
extern HWND g_hScroll;
extern int g_page;
extern std::vector<Widget> g_widgets;
extern int g_scrollY[8];
extern int g_contentH[8];
extern UINT g_dpi;
extern HFONT g_fonts[5];
extern std::wstring g_exeDir;
extern std::wstring g_cfgPath;
extern bool g_micaOn;
extern bool g_unmergedDownload;
extern bool g_playlistRunning;
extern bool g_playlistEmbed;   // Playlist audio only: embed thumbnail + metadata together
extern bool g_audioOnly;       // Audio section: always -x with bestaudio
extern bool g_videoReady;      // a successful Fetch info loaded metadata
extern bool g_playlistReady;   // a successful playlist fetch filled the list
extern bool g_audioReady;      // a successful Fetch info for the Audio section
extern HBRUSH g_brushBg, g_brushPage, g_brushCard, g_brushField, g_brushLog;

struct Tools {
    std::wstring ytdlp, ffmpeg, deno;
    std::wstring ytdlpVer, ffmpegVer, denoVer;
};
extern Tools g_tools;

struct ProgressInfo { int pct; std::wstring text; };

struct MetaResult {
    bool ok = false;
    bool isPlaylist = false;
    std::wstring error;
    std::wstring title, uploader, duration;
    std::wstring subLangs;
    struct Chap { std::wstring title; double start = 0, end = 0; };
    std::vector<Chap> chapters;
    struct Fmt { std::wstring id, ext, res, fps, vcodec, acodec, size, note; };
    std::vector<Fmt> formats;
    struct Ent { std::wstring title, id, duration; };
    std::vector<Ent> entries;
};

// ---- playlist entry state
struct PlaylistEntry {
    std::wstring id;
    std::wstring title;
    std::wstring duration;
    std::wstring url;
    bool selected = true;
    bool done = false;
    bool failed = false;
    std::wstring status;  // pending/downloading/completed/failed
};

extern std::vector<PlaylistEntry> g_playlistEntries;
extern std::wstring g_playlistTitle;   // title of the loaded playlist (for the per-playlist folder)
extern int g_playlistIndex;
extern int g_playlistTotal;
extern int g_playlistCompleted;
extern int g_playlistFailed;
extern bool g_playlistRunning;

enum { RUN_DOWNLOAD = 0, RUN_FETCH = 1, RUN_LIST = 2, RUN_PLAYLIST = 3, RUN_AUDIO = 4, RUN_PL_FETCH = 5 };

// ---- ui helpers
int  Dpi(int v);
int  UnDpi(int v);
void AddWidget(HWND h, int id, int page, int font);
HWND Ctl(int id);
int  PageOfControl(HWND h);
void ApplyFonts();
void RebuildFonts();
void BuildUi(HWND main);
void MainLayout(int w, int h);
void ForceColumnLayout();
void PaintMain(HDC dc, int w, int h);
void LayoutPages();
void ScrollPageTo(int page, int off);
void AnimateScrollBy(int page, int delta);
void SmoothScrollTick();
void CancelSmoothScroll();
void RefreshDownloadUi();
void ShowPlaylistAudioPopup();   // small dark options popup for the Playlist audio toggle
void Place(int id, int x, int y, int w, int h);
void TipAdd(HWND h, const std::wstring& text);
void AppendLog(const std::wstring& s);
void ClearLog();
void SetStatus(const std::wstring& s);
void SetTab(int page);
void UpdateScrollbar();
int  PageAtPoint(HWND h);
HBRUSH BrushForWindow(HWND h);

// ---- playlist / audio page builders + layouts (static in ui.cpp)
// ---- playlist queue management (static in main.cpp)

// ---- dark owner drawing shared with the main window proc
bool   DarkDrawItem(LPDRAWITEMSTRUCT dis);
LRESULT DarkListCustomDraw(LPARAM lp);
bool   IsDarkListControl(HWND h);   // list view or its header
bool   IsCheckCtl(HWND h);          // our owner-drawn checkbox

std::wstring GetCtlText(int id);
void SetCtlText(int id, const std::wstring& s);
int  ComboSel(int id);
void SetComboSel(int id, int i);
int  DefaultComboSel(int id);   // fresh-start selection (1080p / MP4)
bool CtlChecked(int id);
void SetCtlChecked(int id, bool v);
void EnableCtl(int id, bool on);
void SetPct(int pct);

// ---- utilities
std::wstring Utf8ToWide(const std::string& s);std::string  WideToUtf8(const std::wstring& s);
std::wstring QuoteArg(const std::wstring& s);
std::wstring BuildCmdLine(const std::wstring& exe, const std::vector<std::wstring>& args);
std::vector<std::wstring> SplitArgs(const std::wstring& s);
std::wstring Trim(const std::wstring& s);
std::wstring SafeFolderName(const std::wstring& s);  // strips characters Windows forbids in a folder name
std::wstring ExeDir();
std::wstring ParentDir(const std::wstring& p);
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
std::wstring NowStamp();
bool IniSave();
bool IniSaveWindow();         // remembers the window position only (on close)
bool IniLoad();
bool SaveSettings();          // Apply button: write controls to the .txt settings file
bool ResetSettings();         // Reset button: delete the file and restore defaults
std::wstring GetGuiText(const wchar_t* flag);
void LogFmt(const wchar_t* fmt, ...);

// ---- tools / process
void DetectTools();
bool RunCapture(const std::wstring& cmdline, std::string& out, DWORD timeoutMs, DWORD* exitCode);
std::vector<std::wstring> BuildArgs(bool withUrl);
std::wstring EffectiveOutDir();
bool StartYtDlp(const std::vector<std::wstring>& args, int mode);
void CancelYtDlp();
bool IsRunning();
void ProbeToolsAsync();
void UpdateToolsUi();
void StartUpdateCheck();
std::vector<std::wstring> MissingToolPackageIds();
std::wstring WingetCommand(const std::wstring& id, bool upgrade, bool accept, bool includeUnknown);
bool LaunchWingetFor(const std::vector<std::wstring>& ids, bool upgrade);

// ---- json
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;
    const JVal* get(const char* k) const;
};
bool JsonParse(const std::string& src, JVal& out);
void ParseMeta(const std::string& json, MetaResult& m);