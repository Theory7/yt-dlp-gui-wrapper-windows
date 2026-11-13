// yt-dlp option tables + command line builder
#include "app.h"

// Quality / container choices on the Download page
enum { QUAL_BEST = 0, QUAL_2160, QUAL_1440, QUAL_1080, QUAL_720, QUAL_480, QUAL_360, QUAL_240, QUAL_AUDIO, QUAL_CUSTOM };

int PageBase(int page) { return 1000 * (page + 1); }

int OptIdByFlag(const wchar_t* flag) {
    if (!flag || !flag[0]) return 0;
    for (int p = 1; p < g_optPageCount; p++)
        for (int i = 0; i < g_optPages[p].count; i++)
            if (g_optPages[p].defs[i].flag && wcscmp(g_optPages[p].defs[i].flag, flag) == 0)
                return PageBase(p) + i;
    return 0;
}

std::wstring GetGuiText(const wchar_t* flag) {
    int id = OptIdByFlag(flag);
    if (!id) return std::wstring();
    return Trim(GetCtlText(id));
}

// ---------------------------------------------------------------- Settings page
// The app deliberately exposes a small settings page: filename handling, a few
// yt-dlp runtime options, and the winget tool management.
static const OptDef kSettings[] = {
    { OPT_HDR, L"Files", L"", L"", L"", L"" },
    { OPT_CHECK, L"Restrict filenames to ASCII", L"--restrict-filenames", L"", L"", L"Keep only ASCII characters in filenames" },
    { OPT_CHECK, L"Force Windows filenames", L"--windows-filenames", L"", L"", L"Sanitise filenames for Windows" },

    // ---- app-only actions. Flags starting with '!' never reach yt-dlp.
    // Updates are only checked when the user explicitly asks for it.
    { OPT_HDR, L"Tools and updates (app settings)", L"", L"", L"", L"" },
    { OPT_BTN, L"Check for updates now (opens PowerShell)", L"!check-now", L"", L"",
      L"Opens an elevated PowerShell window, checks for tool updates with winget, "
      L"and asks before installing them. Press Enter when it finishes." },
    { OPT_BTN, L"Install missing tools now (opens PowerShell)", L"!install-missing", L"", L"",
      L"Opens an elevated PowerShell window, asks for Windows admin approval, and runs winget install there. "
      L"Press Enter when it finishes; the app restarts." },
};

static const OptDef kNone[] = { { OPT_HDR, L"", L"", L"", L"", L"" } };

const OptPageDef g_optPages[] = {
    { L"Download", kNone, 0, 1 },
    { L"Playlist", kNone, 0, 1 },
    { L"", kNone, 0 },
    { L"Audio", kNone, 0, 1 },
    { L"", kNone, 0 },
    { L"", kNone, 0 },
    { L"Settings", kSettings, (int)(sizeof(kSettings) / sizeof(kSettings[0])) },
};
const int g_optPageCount = (int)(sizeof(g_optPages) / sizeof(g_optPages[0]));

const OptDef* DefByCtlId(int id, int* pageOut, int* idxOut) {
    for (int p = 1; p < g_optPageCount; p++) {
        int base = PageBase(p);
        if (id >= base && id < base + g_optPages[p].count) {
            if (pageOut) *pageOut = p;
            if (idxOut) *idxOut = id - base;
            return &g_optPages[p].defs[id - base];
        }
    }
    return nullptr;
}

std::wstring FmtHeightFilter(int h) {
    if (h <= 0) return L"";
    return L"[height<=" + std::to_wstring(h) + L"]";
}

// Folder that the next job writes to. It follows the active section and adds
// the optional per-playlist subfolder, so BuildArgs and "Open folder" always
// point at the same place.
std::wstring EffectiveOutDir() {
    std::wstring outdir;
    if (g_audioOnly) outdir = Trim(GetCtlText(IDC_AUD_OUTDIR));
    else if (g_playlistRunning || g_page == 1) outdir = Trim(GetCtlText(IDC_PL_OUTDIR));
    if (outdir.empty()) outdir = Trim(GetCtlText(IDC_OUTDIR));
    // "Create folder per playlist" adds one subfolder named after the playlist,
    // so every entry lands inside it instead of being mixed into the base folder.
    // During a real playlist download the folder is always created (the title is
    // normally known; a missing title falls back to "Playlist").
    bool playlistFlow = g_playlistRunning || g_page == 1;
    if (playlistFlow && CtlChecked(IDC_PL_FOLDER) &&
        (g_playlistRunning || !g_playlistTitle.empty())) {
        std::wstring sub = SafeFolderName(g_playlistTitle);
        if (!outdir.empty() && outdir.back() != L'\\' && outdir.back() != L'/') outdir += L'\\';
        outdir += sub;
    }
    return outdir;
}

std::vector<std::wstring> BuildArgs(bool withUrl) {
    std::vector<std::wstring> a;
    a.push_back(L"--color");
    a.push_back(L"never");
    a.push_back(L"--newline");
    a.push_back(L"--progress");
    a.push_back(L"--progress-template");
    a.push_back(L"download:##DL##%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s|"
                L"%(progress.downloaded_bytes)s|%(progress.total_bytes_estimate)s|%(info.title)s");
    a.push_back(L"--progress-template");
    a.push_back(L"postprocess:##PP##%(progress.status)s|%(progress.postprocessor)s");

    int ffmpegEdit = OptIdByFlag(L"--ffmpeg-location");
    bool userFfmpeg = ffmpegEdit && !Trim(GetCtlText(ffmpegEdit)).empty();
    std::wstring ffmpegDir = ParentDir(g_tools.ffmpeg);
    if (!g_tools.ffmpeg.empty() && !ffmpegDir.empty() && !userFfmpeg) {
        a.push_back(L"--ffmpeg-location");
        a.push_back(ffmpegDir);
    }
    int jsEdit = OptIdByFlag(L"--js-runtimes");
    bool userJs = jsEdit && !Trim(GetCtlText(jsEdit)).empty();
    if (!g_tools.deno.empty() && !userJs) {
        a.push_back(L"--js-runtimes");
        // PATH hits are stored as a bare name ("deno.exe"), so let yt-dlp use
        // its own default lookup instead of pointing at a relative path.
        if (ParentDir(g_tools.deno).empty()) a.push_back(L"deno");
        else a.push_back(L"deno:" + g_tools.deno);
    }

    // Pick the output folder that belongs to the active flow: the Audio section
    // has its own folder, Playlist downloads use theirs, everything else uses
    // the Video folder. An empty section folder falls back to the Video one.
    std::wstring outdir = EffectiveOutDir();
    if (!outdir.empty()) {
        a.push_back(L"-P");
        a.push_back(outdir);
    }
    std::wstring tpl = Trim(GetCtlText(IDC_OUTTMPL));
    if (!tpl.empty()) {
        a.push_back(L"-o");
        a.push_back(tpl);
    }

    // ---- format / quality
    // Playlist jobs choose their own quality/container below, so the Video page
    // controls only apply to Video and Audio flows. The codec is not selectable:
    // yt-dlp picks the best stream for the requested height (any codec).
    if (!g_playlistRunning) {
        int q = ComboSel(IDC_QUALITY);
        bool single = CtlChecked(IDC_SINGLE);
        int heights[] = { 0, 2160, 1440, 1080, 720, 480, 360, 240 };
        std::wstring fmt;
        if (g_audioOnly) {
            // The Audio section always extracts the audio stream only; no video
            // is downloaded and no extra prompt is shown.
            fmt = L"ba/b";
            a.push_back(L"--extract-audio");
        } else if (q >= 0 && q <= 7) {
            std::wstring hf = FmtHeightFilter(heights[q]);
            if (single) {
                fmt = L"b" + hf;
                if (!hf.empty()) fmt += L"/b";
            } else {
                // Download separate video + audio streams and let ffmpeg merge them
                // (--merge-output-format is added below when a container is chosen).
                fmt = L"bv*" + hf + L"+ba";
                if (!hf.empty()) fmt += L"/b" + hf;
            }
        }
        if (!fmt.empty()) {
            a.push_back(L"-f");
            a.push_back(fmt);
        }

        int cont = ComboSel(IDC_CONTAINER);
        if (!g_audioOnly && cont >= 1 && cont <= 3) {
            static const wchar_t* cs[] = { L"", L"mp4", L"mkv", L"webm" };
            a.push_back(L"--merge-output-format");
            a.push_back(cs[cont]);
        }
    }

    int pm = ComboSel(IDC_PLAYMODE);
    if (pm == 1) a.push_back(L"--no-playlist");
    else if (pm == 2) a.push_back(L"--yes-playlist");

    // ---- every option exposed on the Settings page
    for (int p = 1; p < g_optPageCount; p++) {
        const OptPageDef& pg = g_optPages[p];
        for (int i = 0; i < pg.count; i++) {
            const OptDef& d = pg.defs[i];
            if (d.flag && d.flag[0] == L'!') continue;  // app-only setting
            int id = PageBase(p) + i;
            HWND h = Ctl(id);
            if (!h) continue;
            if (d.kind == OPT_CHECK) {
                if (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED && d.flag[0]) a.push_back(d.flag);
            } else if (d.kind == OPT_EDIT || d.kind == OPT_FILE) {
                std::wstring v = Trim(GetCtlText(id));
                if (v.empty()) continue;
                if (d.flag[0]) {
                    a.push_back(d.flag);
                    a.push_back(v);
                } else {
                    for (const auto& part : SplitArgs(v)) a.push_back(part);
                }
            } else if (d.kind == OPT_COMBO) {
                int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
                if (sel > 0 && d.flag[0]) {
                    wchar_t buf[1024] = { 0 };
                    SendMessageW(h, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
                    a.push_back(d.flag);
                    a.push_back(buf);
                }
            }
        }
    }

    // ---- audio section (page 3, custom layout). These controls belong to the
    // Audio section only, so they must never leak into Video/Playlist jobs.
    if (g_audioOnly) {
        // The Audio section always saves MP3 and exposes no quality/bitrate
        // choice; yt-dlp's default VBR quality is used. Thumbnail and metadata
        // embedding are always on here: there are no toggles for them anymore.
        a.push_back(L"--audio-format");
        a.push_back(L"mp3");
        a.push_back(L"--embed-thumbnail");
        a.push_back(L"--embed-metadata");
    }

    if (withUrl) {
        std::wstring url = Trim(GetCtlText(IDC_URL));
        if (!url.empty()) a.push_back(url);
    }
    return a;
}
