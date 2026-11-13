// tool discovery + child process plumbing
#include "app.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cwctype>
#include <shellapi.h>

Tools g_tools;

struct RunCtx {
    HANDLE readPipe;
    HANDLE proc;
    HANDLE job;
    int mode;
};

static RunCtx* g_ctx = nullptr;
static volatile LONG g_running = 0;
static volatile LONG g_cancel = 0;
static HANDLE g_procHandle = nullptr;
static HANDLE g_jobHandle = nullptr;

bool IsRunning() { return g_running != 0; }

// ---------- discovery
// PATH first (the user may install or update tools anywhere), then the app
// folder. A PATH hit is stored as a bare command name, so Show command and the
// actual process launch look exactly like typing "yt-dlp" in a terminal.
static std::wstring FindTool(const wchar_t* name) {
    const wchar_t* exts[] = { L".exe", L".cmd", L".bat", L"" };
    std::wstring wname = name;

    // 1) every directory listed in PATH, in order
    wchar_t pathBuf[32768];
    DWORD pathLen = GetEnvironmentVariableW(L"PATH", pathBuf, 32768);
    if (pathLen > 0 && pathLen < 32768) {
        std::wstring path(pathBuf, pathLen);
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find(L';', start);
            std::wstring dir = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            dir = Trim(dir);
            if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"')
                dir = dir.substr(1, dir.size() - 2);
            if (!dir.empty()) {
                for (const wchar_t* e : exts) {
                    std::wstring p = PathJoin(dir, wname + e);
                    if (FileExists(p)) return wname + e;
                }
            }
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }

    // 2) portable layout: next to the exe, bin/, tools/
    std::wstring dirs[3] = { g_exeDir, PathJoin(g_exeDir, L"bin"), PathJoin(g_exeDir, L"tools") };
    for (const auto& d : dirs) {
        for (const wchar_t* e : exts) {
            std::wstring p = PathJoin(d, wname + e);
            if (FileExists(p)) return p;
        }
    }
    return std::wstring();
}

// ---------- capture helper
bool RunCapture(const std::wstring& cmdline, std::string& out, DWORD timeoutMs, DWORD* exitCode) {
    out.clear();
    if (exitCode) *exitCode = (DWORD)-1;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi = { 0 };
    std::wstring cmd = cmdline;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return false;
    }
    char buf[8192];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(rd, buf, sizeof(buf), &got, nullptr) || got == 0) break;
        out.append(buf, got);
        if (out.size() > 32u * 1024u * 1024u) break;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs ? timeoutMs : 120000);
    if (wait == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exitCode) *exitCode = code;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return true;
}

static std::wstring FirstLine(const std::string& s) {
    std::wstring w = Utf8ToWide(s);
    size_t p = w.find_first_of(L"\r\n");
    if (p != std::wstring::npos) w = w.substr(0, p);
    return Trim(w);
}

void DetectTools() {
    g_tools.ytdlp = FindTool(L"yt-dlp");
    if (g_tools.ytdlp.empty()) g_tools.ytdlp = FindTool(L"yt-dlp_x86");
    g_tools.ffmpeg = FindTool(L"ffmpeg");
    g_tools.deno = FindTool(L"deno");

    std::string out;
    DWORD code = 0;
    if (!g_tools.ytdlp.empty()) {
        if (RunCapture(BuildCmdLine(g_tools.ytdlp, { L"--version" }), out, 20000, &code)) {
            std::wstring v = FirstLine(out);
            g_tools.ytdlpVer = v.empty() ? L"?" : v;
        }
    }
    if (!g_tools.ffmpeg.empty()) {
        out.clear();
        if (RunCapture(BuildCmdLine(g_tools.ffmpeg, { L"-version" }), out, 20000, &code)) {
            std::wstring v = FirstLine(out);  // "ffmpeg version 9.0.2-..."
            size_t p = v.find(L"version ");
            if (p != std::wstring::npos) {
                v = v.substr(p + 8);
                size_t sp = v.find(L' ');
                if (sp != std::wstring::npos) v = v.substr(0, sp);
            }
            g_tools.ffmpegVer = v.empty() ? L"?" : v;
        }
    }
    if (!g_tools.deno.empty()) {
        out.clear();
        if (RunCapture(BuildCmdLine(g_tools.deno, { L"--version" }), out, 20000, &code)) {
            std::wstring v = FirstLine(out);  // "deno 2.9.8 (stable, ...)"
            size_t sp = v.find(L' ');
            if (sp != std::wstring::npos) v = v.substr(sp + 1);
            size_t sp2 = v.find(L' ');
            if (sp2 != std::wstring::npos) v = v.substr(0, sp2);
            g_tools.denoVer = v.empty() ? L"?" : v;
        }
    }
}

struct ProbeArg { HWND hwnd; };
static std::wstring ShortVer(const std::wstring& v) {
    size_t p = v.find_first_of(L"-+ ");
    return p == std::wstring::npos ? v : v.substr(0, p);
}
static DWORD WINAPI ProbeThread(LPVOID p) {
    HWND hwnd = (HWND)p;
    std::wstring* s = new std::wstring();
    s->clear();
    *s += L"yt-dlp ";
    *s += g_tools.ytdlpVer.empty() ? L"-" : ShortVer(g_tools.ytdlpVer);
    *s += L"\nffmpeg ";
    *s += g_tools.ffmpegVer.empty() ? L"-" : ShortVer(g_tools.ffmpegVer);
    *s += L"\ndeno ";
    *s += g_tools.denoVer.empty() ? L"-" : ShortVer(g_tools.denoVer);
    PostMessageW(hwnd, WM_APP_LOG, 11, (LPARAM)s);
    return 0;
}

void ProbeToolsAsync() {
    CloseHandle(CreateThread(nullptr, 0, ProbeThread, g_hMain, 0, nullptr));
}

// ---------- async yt-dlp runner
static void PostLogStr(const std::wstring& s, WPARAM tag) {
    PostMessageW(g_hMain, WM_APP_LOG, tag, (LPARAM)new std::wstring(s));
}

static void HandleDownloadLine(const std::wstring& line) {
    if (line.rfind(L"##DL##", 0) == 0) {
        std::wstring rest = line.substr(6);
        std::wstring parts[6];
        int pi = 0;
        for (wchar_t c : rest) {
            if (c == L'|' && pi < 5) { pi++; continue; }
            parts[pi].push_back(c);
        }
        ProgressInfo* info = new ProgressInfo();
        info->pct = -1;
        std::wstring pct = Trim(parts[0]);
        pct.erase(std::remove(pct.begin(), pct.end(), L'%'), pct.end());
        info->pct = (int)_wtof(pct.c_str());
        std::wstring speed = Trim(parts[1]);
        std::wstring eta = Trim(parts[2]);
        info->text = L"Downloading  ";
        info->text += Trim(parts[0]);
        if (!speed.empty() && speed != L"NA") { info->text += L"   "; info->text += speed; }
        if (!eta.empty() && eta != L"NA") { info->text += L"   ETA "; info->text += eta; }
        if (!Trim(parts[5]).empty()) { info->text += L"   "; info->text += Trim(parts[5]); }
        PostMessageW(g_hMain, WM_APP_PROGRESS, 0, (LPARAM)info);
        return;
    }
    if (line.rfind(L"##PP##", 0) == 0) {
        std::wstring rest = line.substr(6);
        size_t bar = rest.find(L'|');
        std::wstring status = bar == std::wstring::npos ? rest : rest.substr(0, bar);
        std::wstring pp = bar == std::wstring::npos ? L"" : rest.substr(bar + 1);
        ProgressInfo* info = new ProgressInfo();
        info->pct = -1;
        info->text = L"Post-processing  " + pp;
        (void)status;
        PostMessageW(g_hMain, WM_APP_PROGRESS, 1, (LPARAM)info);
        return;
    }
    PostLogStr(line, 0);
}

static DWORD WINAPI ReaderThread(LPVOID param) {
    RunCtx* ctx = (RunCtx*)param;
    std::string buf, all;
    char tmp[8192];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(ctx->readPipe, tmp, sizeof(tmp), &got, nullptr) || got == 0) break;
        buf.append(tmp, got);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (ctx->mode == RUN_FETCH || ctx->mode == RUN_PL_FETCH) { all += line; all += '\n'; }
            else if (!line.empty()) HandleDownloadLine(Utf8ToWide(line));
        }
        if (buf.size() > 8u * 1024u * 1024u) buf.clear();
    }
    if (!buf.empty()) {
        if (ctx->mode == RUN_FETCH || ctx->mode == RUN_PL_FETCH) { all += buf; all += '\n'; }
        else HandleDownloadLine(Utf8ToWide(buf));
    }

    // for a fetch, hand the raw JSON to the UI thread
    if (ctx->mode == RUN_FETCH || ctx->mode == RUN_PL_FETCH) {
        MetaResult* m = new MetaResult();
        ParseMeta(all, *m);
        PostMessageW(g_hMain, WM_APP_META, 0, (LPARAM)m);
    }

    WaitForSingleObject(ctx->proc, 5000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(ctx->proc, &code);
    if (g_cancel) code = 0x8000;
    CloseHandle(ctx->readPipe);
    CloseHandle(ctx->proc);
    if (ctx->job) CloseHandle(ctx->job);
    int mode = ctx->mode;
    delete ctx;
    g_ctx = nullptr;
    g_procHandle = nullptr;
    g_jobHandle = nullptr;
    InterlockedExchange(&g_running, 0);
    InterlockedExchange(&g_cancel, 0);
    PostMessageW(g_hMain, WM_APP_RUNDONE, (WPARAM)mode, (LPARAM)code);
    return 0;
}

bool StartYtDlp(const std::vector<std::wstring>& args, int mode) {
    if (IsRunning()) {
        LogFmt(L"[busy] Another yt-dlp job is still running.");
        return false;
    }
    if (g_tools.ytdlp.empty()) {
        LogFmt(L"[error] yt-dlp was not found in PATH or the app folder.");
        return false;
    }
    std::wstring cmd = BuildCmdLine(g_tools.ytdlp, args);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        LogFmt(L"[error] CreatePipe failed (%lu)", GetLastError());
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi = { 0 };
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             g_exeDir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        LogFmt(L"[error] Could not start yt-dlp (%lu)\n%s", GetLastError(), cmd.c_str());
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    CloseHandle(pi.hThread);

    RunCtx* ctx = new RunCtx{ rd, pi.hProcess, job, mode };
    g_ctx = ctx;
    g_procHandle = pi.hProcess;
    g_jobHandle = job;
    InterlockedExchange(&g_running, 1);
    InterlockedExchange(&g_cancel, 0);
    HANDLE th = CreateThread(nullptr, 0, ReaderThread, ctx, 0, nullptr);
    if (!th) {
        LogFmt(L"[error] CreateThread failed (%lu)", GetLastError());
        CancelYtDlp();
        return false;
    }
    CloseHandle(th);
    LogFmt(L"$ %s", cmd.c_str());
    return true;
}

void CancelYtDlp() {
    if (!IsRunning()) return;
    InterlockedExchange(&g_cancel, 1);
    if (g_jobHandle) TerminateJobObject(g_jobHandle, 1);
    if (g_procHandle) TerminateProcess(g_procHandle, 1);
}

// ---------------------------------------------------------------- winget updates
struct ToolPkg { std::wstring id; std::wstring label; };

// The package ids are fixed in the app. They used to be editable Settings
// fields, but a typo there silently broke installation and update checks, so
// they are hardcoded to the official winget packages.
static const wchar_t* kPkgYtDlp = L"yt-dlp.yt-dlp";
static const wchar_t* kPkgFfmpeg = L"Gyan.FFmpeg";
static const wchar_t* kPkgDeno = L"DenoLand.Deno";

// The three packages the app tracks, in the order they are shown.
static std::vector<ToolPkg> ToolPackages() {
    return { { kPkgYtDlp, L"yt-dlp" }, { kPkgFfmpeg, L"FFmpeg" }, { kPkgDeno, L"Deno" } };
}

static void OpenWingetCheckWindow();

// The only update check is the manual one started from Settings: the app never
// checks for tool updates by itself.
void StartUpdateCheck() {
    OpenWingetCheckWindow();
}

// ---------------------------------------------------------------- winget installer
static std::wstring PsQuote(const std::wstring& s) {
    std::wstring r = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') r += L"''";
        else r.push_back(c);
    }
    r += L"'";
    return r;
}

static bool WriteUtf8BomFile(const std::wstring& path, const std::wstring& text) {
    std::string u = WideToUtf8(text);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    WriteFile(f, bom, 3, &written, nullptr);
    WriteFile(f, u.data(), (DWORD)u.size(), &written, nullptr);
    CloseHandle(f);
    return true;
}

// What the app should do once the PowerShell window has closed. The installer
// always restarts (so newly installed tools are found); the update checker only
// restarts when the user actually installed the updates.
enum { WINGET_RESTART_NEVER = 0, WINGET_RESTART_ALWAYS = 1, WINGET_RESTART_IF_INSTALLED = 2 };
static const DWORD kWingetExitInstalled = 10;

struct WingetWaitCtx {
    HANDLE proc = nullptr;
    std::wstring script;
    bool refreshTools = false;
    int restartMode = WINGET_RESTART_NEVER;
};

static DWORD WINAPI WingetWaitThread(LPVOID param) {
    WingetWaitCtx* c = (WingetWaitCtx*)param;
    DWORD exitCode = 0;
    if (c->proc) {
        WaitForSingleObject(c->proc, INFINITE);
        GetExitCodeProcess(c->proc, &exitCode);
        CloseHandle(c->proc);
    }
    if (!c->script.empty()) DeleteFileW(c->script.c_str());
    bool refresh = c->refreshTools;
    bool restart = c->restartMode == WINGET_RESTART_ALWAYS ||
                   (c->restartMode == WINGET_RESTART_IF_INSTALLED && exitCode == kWingetExitInstalled);
    delete c;
    if (restart && g_hMain) PostMessageW(g_hMain, WM_APP_RESTART, 0, 0);
    else if (refresh && g_hMain) PostMessageW(g_hMain, WM_APP_TOOLS, 0, 0);
    return 0;
}

// Launches a generated .ps1 in a normal, visible PowerShell window. The script
// ends with "Press Enter to exit", so pressing Enter closes the window; -NoExit
// is deliberately not used. elevate=true goes through the shell "runas" verb,
// so Windows shows the UAC prompt before anything is installed.
static bool RunPowerShellScript(const std::wstring& path, bool elevate, bool refreshTools,
                                int restartMode) {
    std::wstring params = L"-NoLogo -NoProfile -ExecutionPolicy Bypass -File " + QuoteArg(path);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = elevate ? L"runas" : L"open";
    sei.lpFile = L"powershell.exe";   // classic Windows PowerShell, not pwsh
    sei.lpParameters = params.c_str();
    sei.lpDirectory = g_exeDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        DeleteFileW(path.c_str());
        if (err == ERROR_CANCELLED)
            LogFmt(L"[info] Cancelled at the Windows admin prompt. Nothing was changed.");
        else
            LogFmt(L"[error] Could not open the PowerShell window (code %lu).", err);
        return false;
    }

    WingetWaitCtx* ctx = new WingetWaitCtx();
    ctx->proc = sei.hProcess;
    ctx->script = path;
    ctx->refreshTools = refreshTools;
    ctx->restartMode = restartMode;
    CloseHandle(CreateThread(nullptr, 0, WingetWaitThread, ctx, 0, nullptr));
    return true;
}

// Package ids for whichever required tools are currently missing.
std::vector<std::wstring> MissingToolPackageIds() {
    std::vector<std::wstring> ids;
    if (g_tools.ytdlp.empty()) ids.push_back(kPkgYtDlp);
    if (g_tools.ffmpeg.empty()) ids.push_back(kPkgFfmpeg);
    if (g_tools.deno.empty()) ids.push_back(kPkgDeno);
    return ids;
}

// Shared flags for winget, so the interactive check window and the scripted
// installer always behave the same. --skip-dependencies keeps yt-dlp from
// pulling its own ffmpeg build (yt-dlp.FFmpeg); this app installs the configured
// ffmpeg package directly.
static std::wstring WingetOptions(bool upgrade, bool accept, bool includeUnknown) {
    std::wstring o = L" -e --source winget --skip-dependencies";
    if (upgrade && includeUnknown) o += L" --include-unknown";
    if (accept) o += L" --accept-package-agreements --accept-source-agreements";
    return o;
}

// One winget command line for a package.
std::wstring WingetCommand(const std::wstring& id, bool upgrade, bool accept, bool includeUnknown) {
    std::wstring cmd = upgrade ? L"winget upgrade --id " : L"winget install --id ";
    cmd += PsQuote(id);
    cmd += WingetOptions(upgrade, accept, includeUnknown);
    return cmd;
}

bool LaunchWingetFor(const std::vector<std::wstring>& ids, bool upgrade) {
    if (ids.empty()) {
        LogFmt(L"[info] Nothing to install or update.");
        return false;
    }
    if (FindTool(L"winget").empty()) {
        LogFmt(L"[error] winget was not found. Install or repair the Microsoft App Installer, then try again.");
        return false;
    }

    // Agreements and unknown versions used to be Settings checkboxes. Both are
    // always on now, so the user cannot configure winget into a dead end.
    const bool accept = true;
    const bool includeUnknown = true;
    std::wstring script;
    script += L"$ErrorActionPreference = 'Continue'\r\n";
    script += L"$Host.UI.RawUI.WindowTitle = 'yt-dlp GUI - winget tools'\r\n";
    script += L"Write-Host ''\r\n";
    script += L"Write-Host '  yt-dlp GUI - ";
    script += upgrade ? L"updating tools' " : L"installing required tools' ";
    script += L"-ForegroundColor Cyan\r\n";
    script += L"Write-Host ''\r\n";
    script += L"if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {\r\n";
    script += L"  Write-Host 'winget was not found. Install Microsoft App Installer first.' -ForegroundColor Red\r\n";
    script += L"} else {\r\n";
    for (const auto& id : ids) {
        script += L"  " + WingetCommand(id, upgrade, accept, includeUnknown) + L"\r\n";
        if (upgrade)
            script += L"  if ($LASTEXITCODE -ne 0) { " +
                      WingetCommand(id, false, accept, false) + L" }\r\n";
    }
    script += L"}\r\n";
    script += L"Write-Host ''\r\n";
    script += L"Write-Host 'Finished. Press Enter to exit; yt-dlp GUI will restart.' -ForegroundColor Green\r\n";
    script += L"Read-Host 'Press Enter to exit'\r\n";
    script += L"exit 10\r\n";

    std::wstring path = PathJoin(g_exeDir, L"yt-dlp-gui-winget.ps1");
    if (!WriteUtf8BomFile(path, script)) {
        LogFmt(L"[error] Could not write the installer script next to the app.");
        return false;
    }

    // runas makes Windows show the administrator prompt (UAC) before the
    // elevated PowerShell window opens and runs winget. The window closes when
    // the user presses Enter; the app then closes and reopens itself so the
    // freshly installed tools are detected.
    if (!RunPowerShellScript(path, true, false, WINGET_RESTART_ALWAYS)) return false;

    LogFmt(L"[info] winget is running in an elevated PowerShell window. Approve the Windows admin prompt (UAC).");
    LogFmt(L"[info] Press Enter in that window to exit; the app will restart and pick up the new tools.");
    SetStatus(upgrade ? L"Updating tools with winget..." : L"Installing tools with winget...");
    return true;
}

// "Check for updates now" opens an elevated PowerShell window (installing the
// updates needs administrator rights, so UAC is asked up front). It lists the
// tracked packages and, when updates exist, asks whether to apply them. Every
// path ends with "Press Enter to exit" and closes when Enter is pressed. If the
// user installed the updates the app closes and reopens so the new versions are
// detected; otherwise it simply stays open.
static void OpenWingetCheckWindow() {
    if (FindTool(L"winget").empty()) {
        LogFmt(L"[error] winget was not found. Install or repair the Microsoft App Installer, then try again.");
        SetStatus(L"Update check unavailable");
        return;
    }

    // Agreements and unknown versions are always accepted (these were Settings
    // checkboxes before).
    const bool accept = true;
    const bool includeUnknown = true;

    std::wstring script;
    script += L"$ErrorActionPreference = 'Continue'\r\n";
    script += L"$Host.UI.RawUI.WindowTitle = 'yt-dlp GUI - winget update check'\r\n";
    script += L"Write-Host ''\r\n";
    script += L"Write-Host '  yt-dlp GUI - checking for tool updates' -ForegroundColor Cyan\r\n";
    script += L"Write-Host ''\r\n";
    script += L"$installed = $false\r\n";
    script += L"if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {\r\n";
    script += L"  Write-Host 'winget was not found. Install Microsoft App Installer first.' -ForegroundColor Red\r\n";
    script += L"} else {\r\n";
    script += L"  $targets = @()\r\n";
    // One check per package, by exact id, so each tool is really looked up
    // instead of scanning a single global upgrade table.
    for (const auto& p : ToolPackages()) {
        script += L"  Write-Host '--- " + p.label + L" (" + p.id + L") ---' -ForegroundColor DarkGray\r\n";
        script += L"  $out = winget list --id " + PsQuote(p.id) + L" -e --upgrade-available";
        if (accept) script += L" --accept-source-agreements";
        script += L" 2>&1 | Out-String\r\n";
        script += L"  Write-Host $out\r\n";
        script += L"  foreach ($line in ($out -split '\\r?\\n')) {\r\n";
        script += L"    $t = $line.Trim()\r\n";
        script += L"    if ($t -and ($t -notlike 'No *') -and ($t -like '*" + p.id + L"*')) { $targets += " + PsQuote(p.id) + L"; break }\r\n";
        script += L"  }\r\n";
        script += L"  Write-Host ''\r\n";
    }
    script += L"  if ($targets.Count -eq 0) {\r\n";
    script += L"    Write-Host 'All tracked tools are up to date.' -ForegroundColor Green\r\n";
    script += L"  } else {\r\n";
    script += L"    Write-Host ('Updates available: ' + ($targets -join ', ')) -ForegroundColor Yellow\r\n";
    script += L"    $answer = Read-Host 'Do you want to install these updates now with winget? [y/N]'\r\n";
    script += L"    if ($answer -match '^[Yy]') {\r\n";
    script += L"      $installed = $true\r\n";
    script += L"      foreach ($id in $targets) {\r\n";
    script += L"        Write-Host ''\r\n";
    script += L"        Write-Host ('Updating ' + $id + ' ...') -ForegroundColor Cyan\r\n";
    script += L"        winget upgrade --id $id" + WingetOptions(true, accept, includeUnknown) + L"\r\n";
    script += L"        if ($LASTEXITCODE -ne 0) { winget install --id $id" +
              WingetOptions(false, accept, false) + L" }\r\n";
    script += L"      }\r\n";
    script += L"      Write-Host ''\r\n";
    script += L"      Write-Host 'Updates finished. Check the output above for any errors.' -ForegroundColor Green\r\n";
    script += L"    } else {\r\n";
    script += L"      Write-Host 'No updates were installed.' -ForegroundColor DarkGray\r\n";
    script += L"    }\r\n";
    script += L"  }\r\n";
    script += L"}\r\n";
    script += L"Write-Host ''\r\n";
    script += L"Read-Host 'Press Enter to exit'\r\n";
    script += L"if ($installed) { exit 10 } else { exit 0 }\r\n";

    std::wstring path = PathJoin(g_exeDir, L"yt-dlp-gui-winget-check.ps1");
    if (!WriteUtf8BomFile(path, script)) {
        LogFmt(L"[error] Could not write the update-check script next to the app.");
        return;
    }
    // Elevate up front so the same window can also run the updates.
    if (!RunPowerShellScript(path, true, true, WINGET_RESTART_IF_INSTALLED)) return;

    LogFmt(L"[info] winget is checking for updates in an elevated PowerShell window. Approve the Windows admin prompt (UAC).");
    LogFmt(L"[info] Press Enter in that window to exit. If updates were installed the app restarts so it sees the new versions.");
    SetStatus(L"Checking for tool updates in PowerShell...");
}
