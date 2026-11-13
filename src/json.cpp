// tiny JSON reader (no dependencies)
#include "app.h"
#include <cstdlib>
#include <cmath>

const JVal* JVal::get(const char* k) const {
    if (t != OBJ) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == k) return &kv.second;
    return nullptr;
}

namespace {

struct Parser {
    const char* s;
    size_t n, i;
    int depth = 0;

    void ws() {
        while (i < n) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
            else break;
        }
    }
    bool hex4(unsigned& out) {
        if (i + 4 > n) return false;
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        i += 4;
        out = v;
        return true;
    }
    static void putUtf8(unsigned cp, std::string& o) {
        if (cp < 0x80) { o += (char)cp; }
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool str(std::string& o) {
        if (i >= n || s[i] != '"') return false;
        i++;
        o.clear();
        while (i < n) {
            unsigned char c = (unsigned char)s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= n) return false;
                char e = s[i++];
                switch (e) {
                    case '"': o += '"'; break;
                    case '\\': o += '\\'; break;
                    case '/': o += '/'; break;
                    case 'b': o += '\b'; break;
                    case 'f': o += '\f'; break;
                    case 'n': o += '\n'; break;
                    case 'r': o += '\r'; break;
                    case 't': o += '\t'; break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && s[i] == '\\' && s[i + 1] == 'u') {
                            i += 2;
                            unsigned lo = 0;
                            if (!hex4(lo)) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        putUtf8(cp, o);
                        break;
                    }
                    default: return false;
                }
            } else {
                o += (char)c;
            }
        }
        return false;
    }
    bool value(JVal& v) {
        if (++depth > 200) return false;
        ws();
        if (i >= n) { depth--; return false; }
        char c = s[i];
        bool ok = false;
        if (c == '{') {
            i++;
            v.t = JVal::OBJ;
            ws();
            if (i < n && s[i] == '}') { i++; ok = true; }
            else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!str(key)) break;
                    ws();
                    if (i >= n || s[i] != ':') break;
                    i++;
                    JVal child;
                    if (!value(child)) break;
                    v.obj.emplace_back(std::move(key), std::move(child));
                    ws();
                    if (i < n && s[i] == ',') { i++; continue; }
                    if (i < n && s[i] == '}') { i++; ok = true; }
                    break;
                }
            }
        } else if (c == '[') {
            i++;
            v.t = JVal::ARR;
            ws();
            if (i < n && s[i] == ']') { i++; ok = true; }
            else {
                for (;;) {
                    JVal child;
                    if (!value(child)) break;
                    v.arr.push_back(std::move(child));
                    ws();
                    if (i < n && s[i] == ',') { i++; continue; }
                    if (i < n && s[i] == ']') { i++; ok = true; }
                    break;
                }
            }
        } else if (c == '"') {
            v.t = JVal::STR;
            ok = str(v.s);
        } else if (c == 't' && i + 4 <= n && !strncmp(s + i, "true", 4)) {
            i += 4; v.t = JVal::BOOL; v.b = true; ok = true;
        } else if (c == 'f' && i + 5 <= n && !strncmp(s + i, "false", 5)) {
            i += 5; v.t = JVal::BOOL; v.b = false; ok = true;
        } else if (c == 'n' && i + 4 <= n && !strncmp(s + i, "null", 4)) {
            i += 4; v.t = JVal::NUL; ok = true;
        } else {
            const char* start = s + i;
            char* end = nullptr;
            double d = strtod(start, &end);
            if (end && end > start) {
                i += (size_t)(end - start);
                v.t = JVal::NUM; v.num = d; ok = true;
            }
        }
        depth--;
        return ok;
    }
};

std::wstring numToW(double d) {
    if (!(d == d)) return L"";
    if (d == (double)(long long)d) {
        wchar_t b[32];
        swprintf(b, 32, L"%lld", (long long)d);
        return b;
    }
    wchar_t b[48];
    swprintf(b, 48, L"%.3f", d);
    return b;
}

std::wstring fmtTime(double sec) {
    if (!(sec >= 0)) return L"";
    long long t = (long long)(sec + 0.5);
    wchar_t b[32];
    swprintf(b, 32, L"%02lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    return b;
}

std::wstring fmtBytes(double v) {
    if (!(v > 0)) return L"";
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int k = 0;
    while (v >= 1024.0 && k < 4) { v /= 1024.0; k++; }
    wchar_t b[48];
    swprintf(b, 48, L"%.1f %s", v, u[k]);
    return b;
}

std::wstring strOf(const JVal* v) {
    if (!v) return L"";
    switch (v->t) {
        case JVal::STR: return Utf8ToWide(v->s);
        case JVal::NUM: return numToW(v->num);
        case JVal::BOOL: return v->b ? L"yes" : L"no";
        default: return L"";
    }
}

}  // namespace

bool JsonParse(const std::string& src, JVal& out) {
    Parser p{ src.c_str(), src.size(), 0, 0 };
    out = JVal();
    if (!p.value(out)) return false;
    p.ws();
    // trailing garbage tolerated: yt-dlp may append lines after the document
    return true;
}

void ParseMeta(const std::string& json, MetaResult& m) {
    m = MetaResult();
    size_t a = json.find('{');
    size_t b = json.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) {
        m.error = L"No JSON in yt-dlp output";
        return;
    }
    JVal root;
    if (!JsonParse(json.substr(a, b - a + 1), root)) {
        m.error = L"Could not parse yt-dlp JSON";
        return;
    }
    if (root.t != JVal::OBJ) { m.error = L"Unexpected JSON root"; return; }
    if (const JVal* e = root.get("_type"))
        if (e->t == JVal::STR && e->s == "playlist") m.isPlaylist = true;
    if (m.title.empty()) m.title = strOf(root.get("title"));
    if (m.uploader.empty()) m.uploader = strOf(root.get("uploader"));
    if (m.uploader.empty()) m.uploader = strOf(root.get("channel"));
    double dur = 0;
    if (const JVal* d = root.get("duration")) if (d->t == JVal::NUM) dur = d->num;
    m.duration = fmtTime(dur);

    if (const JVal* ch = root.get("chapters")) {
        if (ch->t == JVal::ARR) {
            for (const auto& c : ch->arr) {
                MetaResult::Chap cc;
                cc.title = strOf(c.get("title"));
                if (cc.title.empty()) cc.title = L"(chapter)";
                if (const JVal* st = c.get("start_time")) if (st->t == JVal::NUM) cc.start = st->num;
                if (const JVal* en = c.get("end_time")) if (en->t == JVal::NUM) cc.end = en->num;
                m.chapters.push_back(cc);
            }
        }
    }
    if (const JVal* f = root.get("formats")) {
        if (f->t == JVal::ARR) {
            for (const auto& x : f->arr) {
                if (x.t != JVal::OBJ) continue;
                MetaResult::Fmt r;
                r.id = strOf(x.get("format_id"));
                r.ext = strOf(x.get("ext"));
                std::wstring h = strOf(x.get("height"));
                std::wstring w = strOf(x.get("width"));
                std::wstring res = strOf(x.get("resolution"));
                if (!res.empty() && res != L"audio only") r.res = res;
                else if (!h.empty() && h != L"0") r.res = h + L"p";
                else if (!w.empty() && w != L"0") r.res = w + L"x?";
                if (r.res.empty()) r.res = strOf(x.get("format_note"));
                r.fps = strOf(x.get("fps"));
                r.vcodec = strOf(x.get("vcodec"));
                r.acodec = strOf(x.get("acodec"));
                double sz = 0;
                if (const JVal* s = x.get("filesize")) if (s->t == JVal::NUM) sz = s->num;
                if (sz <= 0) if (const JVal* s = x.get("filesize_approx")) if (s->t == JVal::NUM) sz = s->num;
                r.size = fmtBytes(sz);
                r.note = strOf(x.get("format_note"));
                if (r.vcodec == L"none") r.vcodec = L"-";
                if (r.acodec == L"none") r.acodec = L"-";
                m.formats.push_back(r);
            }
        }
    }
    if (const JVal* ents = root.get("entries")) {
        if (ents->t == JVal::ARR) {
            m.isPlaylist = true;
            for (const auto& e : ents->arr) {
                if (e.t != JVal::OBJ) continue;
                MetaResult::Ent en;
                en.title = strOf(e.get("title"));
                en.id = strOf(e.get("id"));
                double d = 0;
                if (const JVal* dv = e.get("duration")) if (dv->t == JVal::NUM) d = dv->num;
                en.duration = fmtTime(d);
                if (en.title.empty()) en.title = en.id.empty() ? L"(entry)" : en.id;
                m.entries.push_back(en);
            }
        }
    }
    if (const JVal* subs = root.get("subtitles")) {
        if (subs->t == JVal::OBJ) {
            for (const auto& kv : subs->obj) {
                if (!m.subLangs.empty()) m.subLangs += L",";
                m.subLangs += Utf8ToWide(kv.first);
            }
        }
    }
    m.ok = true;
}
