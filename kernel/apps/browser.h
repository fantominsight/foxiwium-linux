#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../gui/window.h"
#include "../graphics/graphics.h"
#include "../graphics/font.h"
#include "../drivers/net.h"
#include "../drivers/ps2_keyboard.h"
#include "../drivers/ps2_mouse.h"
#include "../fs/vfs.h"

namespace browser {

constexpr int MAX_URL     = 256;
constexpr int MAX_SOURCE  = 30000;
constexpr int MAX_PLAIN   = 30000;
constexpr int MAX_LINKS   = 96;
constexpr int MAX_HISTORY = 32;
constexpr int MAX_LINES   = 2000;

constexpr int TOOLBAR_H = 34;
constexpr int STATUS_H  = 22;
constexpr int LINE_H    = 14;

constexpr const char* DEFAULT_URL = "file:///home/user/Documents/wiki.html";

inline bool starts_with(const char* s, const char* p) {
    while (*p) { if (*s != *p) return false; s++; p++; }
    return true;
}

struct RawLink {
    int line;
    int c0, c1;
    char url[MAX_URL];
};

struct ScreenLink {
    int line;
    int x0, x1;
    char url[MAX_URL];
};

struct BrowserState {
    char url[MAX_URL];        // address bar text
    int url_len;
    char current[MAX_URL];    // URL actually displayed
    char source[MAX_SOURCE];  // raw HTTP body
    int source_len;
    char plain[MAX_PLAIN];    // stripped text (logical lines)
    int plain_len;
    RawLink raw[MAX_LINKS];
    int raw_count;
    char layout[MAX_PLAIN];   // word-wrapped screen lines
    int layout_len;
    int line_start[MAX_LINES];
    int line_len[MAX_LINES];
    int line_count;
    ScreenLink links[MAX_LINKS];
    int link_count;
    char history[MAX_HISTORY][MAX_URL];
    int history_pos;
    int history_len;
    int scroll;
    int max_scroll;
    int wrap_cols;
    char status[192];
    int status_tick;
    bool loading;
    bool started;
    bool url_focused;
    int hover_btn;   // -1 none, 0 back, 1 fwd, 2 reload, 3 go
};

static BrowserState s;

// ---------------- string helpers ----------------
inline int s_len(const char* p) { int l = 0; while (p[l]) l++; return l; }

inline void s_copy(char* dst, int max, const char* src) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

inline bool tag_eq(const char* a, const char* b) {
    int i = 0;
    while (b[i]) {
        char ca = a[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (ca != b[i]) return false;
        i++;
    }
    return true;
}

inline bool is_block_tag(const char* t) {
    return tag_eq(t, "p")    || tag_eq(t, "div")  || tag_eq(t, "br")   ||
           tag_eq(t, "h1")   || tag_eq(t, "h2")   || tag_eq(t, "h3")   ||
           tag_eq(t, "h4")   || tag_eq(t, "h5")   || tag_eq(t, "h6")   ||
           tag_eq(t, "li")   || tag_eq(t, "ul")   || tag_eq(t, "ol")   ||
           tag_eq(t, "tr")   || tag_eq(t, "td")   || tag_eq(t, "th")   ||
           tag_eq(t, "table")|| tag_eq(t, "pre")  || tag_eq(t, "hr")   ||
           tag_eq(t, "blockquote") || tag_eq(t, "article") ||
           tag_eq(t, "section")    || tag_eq(t, "header")  ||
           tag_eq(t, "footer")     || tag_eq(t, "form");
}

// ---------------- URL helpers ----------------
inline bool url_is_http(const char* u) {
    if (u[0] != 'h' || u[1] != 't' || u[2] != 't' || u[3] != 'p') return false;
    const char* p = u + 4;
    if (*p == 's') p++;           // https://
    return (p[0] == ':' && p[1] == '/' && p[2] == '/');
}

inline void resolve_url(const char* base, const char* href, char* out, int max) {
    if (url_is_http(href) || starts_with(href, "file://")) {
        s_copy(out, max, href);
        return;
    }
    if (href[0] == '/' && href[1] == '/') {
        // protocol-relative: keep the base scheme
        int i = 0;
        if (starts_with(base, "https://")) {
            const char* pre = "https:";
            while (*pre && i < max - 1) out[i++] = *pre++;
        } else {
            const char* pre = "http:";
            while (*pre && i < max - 1) out[i++] = *pre++;
        }
        while (*href && i < max - 1) out[i++] = *href++;
        out[i] = 0;
        return;
    }
    if (href[0] == '/') {
        // base scheme://host + path
        int i = 0;
        const char* p = base;
        int slash = 0;
        while (*p && i < max - 1) {
            if (*p == '/') slash++;
            out[i++] = *p++;
            if (slash == 3) break;   // after "http://host"
        }
        int j = 0;
        while (href[j] && i < max - 1) out[i++] = href[j++];
        out[i] = 0;
        return;
    }
    // relative: take base up to last '/'
    int last = -1;
    for (int i = 0; base[i]; i++) if (base[i] == '/') last = i;
    int i = 0;
    for (int k = 0; k <= last && i < max - 1; k++) out[i++] = base[k];
    int j = 0;
    while (href[j] && i < max - 1) out[i++] = href[j++];
    out[i] = 0;
}

// ---------------- search helpers ----------------
inline bool contains_sub(const char* s, const char* sub) {
    for (int i = 0; s[i]; i++) {
        int j = 0;
        while (sub[j] && s[i + j] && s[i + j] == sub[j]) j++;
        if (!sub[j]) return true;
    }
    return false;
}

// Percent-encode a UTF-8 query string (raw bytes) for a URL. Spaces -> '+'.
inline int url_encode(const char* in, char* out, int max) {
    int o = 0;
    const char* hexd = "0123456789ABCDEF";
    for (int i = 0; in[i] && o < max - 3; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ') {
            if (o < max - 1) out[o++] = '+';
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hexd[c >> 4];
            out[o++] = hexd[c & 15];
        }
    }
    out[o] = 0;
    return o;
}

// Is this something to browse directly (scheme / dotted host) or a search query?
inline bool looks_like_url(const char* u) {
    if (contains_sub(u, "://")) return true;
    int dots = 0, spaces = 0;
    for (int i = 0; u[i]; i++) {
        if (u[i] == '.') dots++;
        if (u[i] == ' ') spaces++;
    }
    return dots > 0 && spaces == 0;
}

inline bool is_bing_rss(const char* url) {
    return contains_sub(url, "format=rss");
}

// Convert a Bing RSS search response into HTML anchors so extract() can
// render the results as clickable links. The RSS format is compact
// (<4KB for 10 results) and carries direct result URLs.
inline int rss_to_html(const char* src, int n, char* out, int max) {
    int o = 0;
    auto put = [&](const char* t) {
        while (*t && o < max - 1) out[o++] = *t++;
    };
    int i = 0;
    while (i + 6 <= n) {
        if (src[i] != '<' || src[i + 1] != 'i' || src[i + 2] != 't' ||
            src[i + 3] != 'e' || src[i + 4] != 'm' || src[i + 5] != '>') {
            i++;
            continue;
        }
        i += 6;
        char title[MAX_URL]; int tl = 0;
        char link[MAX_URL];  int ll = 0;
        char desc[512];      int dl = 0;
        while (i + 7 <= n && !(src[i] == '<' && src[i + 1] == '/' &&
                               src[i + 2] == 'i' && src[i + 3] == 't' &&
                               src[i + 4] == 'e' && src[i + 5] == 'm' &&
                               src[i + 6] == '>')) {
            if (src[i] == '<' && src[i + 1] == 't' && src[i + 2] == 'i' &&
                src[i + 3] == 't' && src[i + 4] == 'l' && src[i + 5] == 'e' &&
                src[i + 6] == '>') {
                i += 7;
                while (i + 2 <= n && !(src[i] == '<' && src[i + 1] == '/')) {
                    if (tl < MAX_URL - 1) title[tl++] = src[i];
                    i++;
                }
                title[tl] = 0;
            } else if (src[i] == '<' && src[i + 1] == 'l' && src[i + 2] == 'i' &&
                       src[i + 3] == 'n' && src[i + 4] == 'k' && src[i + 5] == '>') {
                i += 6;
                while (i + 2 <= n && !(src[i] == '<' && src[i + 1] == '/')) {
                    if (ll < MAX_URL - 1) link[ll++] = src[i];
                    i++;
                }
                link[ll] = 0;
            } else if (src[i] == '<' && src[i + 1] == 'd' && src[i + 2] == 'e' &&
                       src[i + 3] == 's' && src[i + 4] == 'c' && src[i + 5] == 'r' &&
                       src[i + 6] == 'i' && src[i + 7] == 'p' && src[i + 8] == 't' &&
                       src[i + 9] == 'i' && src[i + 10] == 'o' && src[i + 11] == 'n' &&
                       src[i + 12] == '>') {
                i += 13;
                while (i + 2 <= n && !(src[i] == '<' && src[i + 1] == '/')) {
                    if (dl < 511) desc[dl++] = src[i];
                    i++;
                }
                desc[dl] = 0;
            } else {
                i++;
            }
        }
        i += 7;
        if (ll > 0) {
            put("<div><a href=\"");
            for (int k = 0; k < ll && o < max - 1; k++) out[o++] = link[k];
            put("\">");
            for (int k = 0; k < tl && o < max - 1; k++) out[o++] = title[k];
            put("</a><br>");
            for (int k = 0; k < dl && o < max - 1; k++) out[o++] = desc[k];
            put("</div>\n");
        }
    }
    out[o] = 0;
    return o;
}

// ---------------- extraction (HTML -> plain text + links) ----------------
static bool match_ic(const char* p, const char* w) {
    int i = 0;
    while (w[i]) {
        char ca = p[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (ca != w[i]) return false;
        i++;
    }
    return true;
}

static void parse_href(const char* tag, int taglen, char* out, int max) {
    out[0] = 0;
    int i = 0;
    while (i + 4 < taglen) {
        if ((tag[i] == 'h' || tag[i] == 'H') &&
            (tag[i + 1] == 'r' || tag[i + 1] == 'R') &&
            (tag[i + 2] == 'e' || tag[i + 2] == 'E') &&
            (tag[i + 3] == 'f' || tag[i + 3] == 'F')) {
            int j = i + 4;
            while (j < taglen && tag[j] != '=') j++;
            if (j >= taglen) return;
            j++;
            while (j < taglen && (tag[j] == ' ' || tag[j] == '\t')) j++;
            char q = tag[j];
            if (q != '"' && q != '\'') q = 0;
            if (q) j++;
            int o = 0;
            while (j < taglen && (q ? tag[j] != q : (tag[j] != ' ' && tag[j] != '>')) &&
                   o < max - 1) {
                out[o++] = tag[j++];
            }
            out[o] = 0;
            // strip whitespace in attribute value
            while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) out[--o] = 0;
            return;
        }
        i++;
    }
}

static bool parse_entity(const char* p, int n, char* one) {
    // p points at '&'; match up to ';'
    int end = -1;
    for (int i = 1; i < n && i < 10; i++) {
        if (p[i] == ';') { end = i; break; }
    }
    if (end < 0) return false;
    int len = end - 1;  // chars between & and ;
    if (len == 0) return false;
    if (p[1] == '#') {
        int num = 0;
        int k = 2;
        while (k < end) {
            char c = p[k];
            if (c < '0' || c > '9') return false;
            num = num * 10 + (c - '0');
            k++;
        }
        switch (num) {
            case 38: *one = '&'; break;
            case 60: *one = '<'; break;
            case 62: *one = '>'; break;
            case 34: *one = '"'; break;
            case 39: *one = '\''; break;
            case 160: *one = ' '; break;
            default: *one = ' '; break;
        }
        return true;
    }
    if (len == 3 && match_ic(p + 1, "amp")) { *one = '&'; return true; }
    if (len == 2 && match_ic(p + 1, "lt")) { *one = '<'; return true; }
    if (len == 2 && match_ic(p + 1, "gt")) { *one = '>'; return true; }
    if (len == 4 && match_ic(p + 1, "quot")) { *one = '"'; return true; }
    if (len == 3 && match_ic(p + 1, "apos")) { *one = '\''; return true; }
    if (len == 4 && match_ic(p + 1, "nbsp")) { *one = ' '; return true; }
    return false;
}

static void extract() {
    s.plain_len = 0;
    s.raw_count = 0;
    int line = 0, col = 0;
    bool in_script = false, in_style = false;
    bool in_tag = false;
    RawLink pending;
    bool have_pending = false;
    pending.line = 0; pending.c0 = 0; pending.c1 = 0;
    pending.url[0] = 0;

    auto emit_char = [&](char c) {
        if (s.plain_len >= MAX_PLAIN - 1) return;
        if (c == '\n') {
            if (s.plain_len > 0 && s.plain[s.plain_len - 1] != '\n') {
                s.plain[s.plain_len++] = '\n';
                line++;
                col = 0;
            }
            return;
        }
        if (c == ' ') {
            if (s.plain_len > 0 && s.plain[s.plain_len - 1] == ' ') return;
            if (col == 0) return;
        }
        s.plain[s.plain_len++] = c;
        col++;
    };

    auto close_pending = [&]() {
        if (have_pending && s.raw_count < MAX_LINKS) {
            RawLink& r = s.raw[s.raw_count++];
            r.line = pending.line;
            r.c0 = pending.c0;
            r.c1 = col;
            s_copy(r.url, MAX_URL, pending.url);
        }
        have_pending = false;
    };

    const char* p = s.source;
    int n = s.source_len;
    int i = 0;
    while (i < n) {
        char c = p[i];

        if (in_tag) {
            if (c == '>') in_tag = false;
            i++;
            continue;
        }
        if (in_script) {
            if (c == '<' && match_ic(p + i, "</script")) {
                in_script = false;
                in_tag = true;
                i++;
                continue;
            }
            i++;
            continue;
        }
        if (in_style) {
            if (c == '<' && match_ic(p + i, "</style")) {
                in_style = false;
                in_tag = true;
                i++;
                continue;
            }
            i++;
            continue;
        }

        if (c == '<') {
            int j = i + 1;
            if (j >= n) break;
            bool closing = (p[j] == '/');
            if (closing) j++;
            int k = j;
            while (k < n && p[k] != '>' && p[k] != ' ' && p[k] != '/' &&
                   p[k] != '\t' && p[k] != '\n') k++;
            char tname[16];
            int tl = k - j;
            if (tl > 15) tl = 15;
            for (int z = 0; z < tl; z++) tname[z] = p[j + z];
            tname[tl] = 0;

            if (tag_eq(tname, "script") && !closing) {
                in_script = true;
                i = k;
                continue;
            }
            if (tag_eq(tname, "style") && !closing) {
                in_style = true;
                i = k;
                continue;
            }
            if (tag_eq(tname, "a") && !closing) {
                int te = i + 1;
                while (te < n && p[te] != '>') te++;
                int taglen = te - (i + 1);
                if (taglen > 300) taglen = 300;
                char href[MAX_URL];
                parse_href(p + i + 1, taglen, href, MAX_URL);
                if (have_pending) close_pending();
                if (href[0]) {
                    have_pending = true;
                    pending.line = line;
                    pending.c0 = col;
                    pending.c1 = col;
                    s_copy(pending.url, MAX_URL, href);
                }
                i = (te < n) ? te + 1 : te;
                continue;
            }
            if (tag_eq(tname, "a") && closing) {
                close_pending();
                int te = i + 1;
                while (te < n && p[te] != '>') te++;
                i = (te < n) ? te + 1 : te;
                continue;
            }
            bool block = is_block_tag(tname);
            int te = i + 1;
            while (te < n && p[te] != '>') te++;
            i = (te < n) ? te + 1 : te;
            if (block) emit_char('\n');
            continue;
        }

        if (c == '&') {
            char one = 0;
            if (parse_entity(p + i, n - i, &one)) {
                emit_char(one);
                int semi = i + 1;
                while (semi < n && p[semi] != ';') semi++;
                i = semi + 1;
                continue;
            }
        }

        if (c == '\r' || c == '\t' || c == '\n') {
            emit_char(' ');
        } else if (c >= 32 && c < 127) {
            emit_char(c);
        } else {
            emit_char(' ');
        }
        i++;
    }
    close_pending();
    s.plain[s.plain_len] = 0;
}

// ---------------- word wrapping ----------------
static void rewrap(int cols) {
    if (cols < 4) cols = 4;
    s.wrap_cols = cols;
    s.layout_len = 0;
    s.line_count = 0;
    s.link_count = 0;

    int ci = 0;
    int ll = 0;
    while (ci <= s.plain_len) {
        int le = ci;
        while (le < s.plain_len && s.plain[le] != '\n') le++;
        int llen = le - ci;
        int p = 0;
        while (p < llen) {
            int q = p;
            int last_space = -1;
            while (q < llen && (q - p) < cols) {
                if (s.plain[ci + q] == ' ') last_space = q;
                q++;
            }
            int seg_end = q;
            if (q < llen && (q - p) == cols && last_space > p) {
                seg_end = last_space;
            }
            if (seg_end == p) seg_end = (p + 1 <= llen) ? p + 1 : p;
            int slen = seg_end - p;

            if (s.line_count < MAX_LINES && s.layout_len + slen + 1 < MAX_PLAIN) {
                s.line_start[s.line_count] = s.layout_len;
                for (int k = 0; k < slen; k++) s.layout[s.layout_len++] = s.plain[ci + p + k];
                s.layout[s.layout_len++] = '\n';
                s.line_len[s.line_count] = slen;
                s.line_count++;
            }

            for (int li = 0; li < s.raw_count && s.link_count < MAX_LINKS; li++) {
                if (s.raw[li].line != ll) continue;
                int ov0 = p > s.raw[li].c0 ? p : s.raw[li].c0;
                int ov1 = seg_end < s.raw[li].c1 ? seg_end : s.raw[li].c1;
                if (ov0 < ov1) {
                    ScreenLink& sl = s.links[s.link_count++];
                    sl.line = s.line_count - 1;
                    sl.x0 = (ov0 - p) * 8;
                    sl.x1 = (ov1 - p) * 8;
                    s_copy(sl.url, MAX_URL, s.raw[li].url);
                }
            }

            p = seg_end;
            while (p < llen && s.plain[ci + p] == ' ') p++;
        }
        ci = le + 1;
        ll++;
    }
    s.layout[s.layout_len] = 0;
}

// ---------------- page loading ----------------
static void set_status(const char* msg) {
    s_copy(s.status, 192, msg);
    s.status_tick = 0;
}

static void show_error(const char* url, int code);

static void load_local(const char* raw_path) {
    net::http_cancel();
    s.loading = false;
    const char* path = raw_path;
    if (starts_with(path, "file://")) path += 7;
    s_copy(s.current, MAX_URL, path);

    int idx = vfs::resolve_path(path);
    if (idx < 0) {
        s.source_len = 0;
        s.scroll = 0;
        set_status("Not found");
        show_error(path, -99);
        return;
    }
    vfs::VfsNode& n = vfs::nodes[idx];
    if (n.type == vfs::NODE_DIR) {
        char buf[MAX_SOURCE];
        int o = 0;
        auto put = [&](const char* t) {
            while (*t && o < MAX_SOURCE - 1) buf[o++] = *t++;
        };
        put("<html><body><h1>Index of ");
        put(path);
        put("</h1><ul>");
        for (int ci = 0; ci < n.child_count; ci++) {
            int cidx = n.children[ci];
            if (cidx < 0 || cidx >= vfs::node_count) continue;
            const char* name = vfs::nodes[cidx].name;
            if (name[0] == '.') continue;
            put("<li><a href=\"file://");
            put(path);
            if (o > 0 && buf[o - 1] != '/') put("/");
            put(name);
            put("\">");
            put(name);
            if (vfs::nodes[cidx].type == vfs::NODE_DIR) put("/");
            put("</a></li>");
        }
        put("</ul></body></html>");
        buf[o] = 0;
        s.source_len = o;
        for (int i = 0; i <= o; i++) s.source[i] = buf[i];
        extract();
        rewrap(s.wrap_cols);
        s.scroll = 0;
        set_status("");
        return;
    }
    int sz = (int)n.content_size;
    if (sz > MAX_SOURCE - 1) sz = MAX_SOURCE - 1;
    for (int i = 0; i < sz; i++) s.source[i] = n.content[i];
    s.source_len = sz;
    s.source[sz] = 0;
    if (sz == 0) {
        s.scroll = 0;
        set_status("Empty file");
        show_error(path, -98);
        return;
    }
    extract();
    rewrap(s.wrap_cols);
    s.scroll = 0;
    set_status("");
}

static void show_error(const char* url, int code) {
    int o = 0;
    auto put = [&](const char* t) {
        while (*t && o < MAX_SOURCE - 1) s.plain[o++] = *t++;
    };
    put("Foxiwium Browser\n\nUnable to load:\n  ");
    put(url);
    put("\n\nError code: -");
    char nb[8];
    int ni = 0;
    int v = -code;
    if (v == 0) nb[ni++] = '0';
    while (v > 0) { nb[ni++] = '0' + v % 10; v /= 10; }
    nb[ni] = 0;
    for (int i = 0; i < ni; i++) s.plain[o++] = nb[i];
    s.plain[o] = 0;
    s.plain_len = o;
    s.raw_count = 0;
    rewrap(s.wrap_cols);
}

static void load(const char* url) {
    net::http_cancel();
    s_copy(s.current, MAX_URL, url);
    s_copy(s.url, MAX_URL, url);
    s.url_len = s_len(url);

    s.loading = true;
    set_status("Connecting...");
    net::http_begin(url, s.source, MAX_SOURCE - 1);
}

// Route a navigation to either a local file or the network.
// load_local() is synchronous (VFS), network loads are async.
static void start_load(const char* url) {
    if (starts_with(url, "file://") || url[0] == '/' || url[0] == '.') {
        load_local(url);
        return;
    }
    load(url);
}

// Called by the desktop loop every frame while a fetch is pending.
// Advances the job by a bounded slice and finishes the page when done.
// Returns true when the page state changed and needs a redraw.
inline bool update() {
    if (!s.loading) return false;
    int r = net::http_poll(8000);
    if (r == 0) return true;   // still running: keep repainting the spinner

    s.loading = false;
    if (r < 0) {
        s.source_len = 0;
        s.scroll = 0;
        set_status("Failed to load page");
        show_error(s.current, r);
        return true;
    }
    s.source_len = r;
    s.source[r] = 0;
    if (is_bing_rss(s.current)) {
        static char conv[MAX_SOURCE];
        int cn = rss_to_html(s.source, r, conv, MAX_SOURCE);
        for (int i = 0; i <= cn; i++) s.source[i] = conv[i];
        s.source_len = cn;
    }
    extract();
    rewrap(s.wrap_cols);
    s.scroll = 0;
    char st[96];
    int so = 0;
    const char* pre = "Loaded ";
    while (*pre && so < 95) st[so++] = *pre++;
    int v = r;
    char tmp[16]; int ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; }
    while (ti > 0) st[so++] = tmp[--ti];
    st[so++] = ' ';
    st[so++] = 'b';
    st[so++] = 'y';
    st[so++] = 't';
    st[so++] = 'e';
    st[so++] = 's';
    st[so] = 0;
    set_status(st);
    return true;
}

static void push_history(const char* url) {
    if (s.history_len > 0 && s.history_pos >= 0 && s.history_pos < s.history_len &&
        s_len(s.history[s.history_pos]) == s_len(url)) {
        bool same = true;
        for (int i = 0; url[i]; i++) {
            if (s.history[s.history_pos][i] != url[i]) { same = false; break; }
        }
        if (same) return;
    }
    s.history_pos++;
    s.history_len = s.history_pos + 1;
    if (s.history_len > MAX_HISTORY) {
        for (int i = 1; i < s.history_len; i++) {
            s_copy(s.history[i - 1], MAX_URL, s.history[i]);
        }
        s.history_len--;
        s.history_pos--;
    }
    s_copy(s.history[s.history_pos], MAX_URL, url);
}

static void navigate(const char* raw) {
    char u[MAX_URL];
    s_copy(u, MAX_URL, raw);
    // trim leading spaces
    int a = 0;
    while (u[a] == ' ') a++;
    int b = 0;
    while (u[a]) u[b++] = u[a++];
    u[b] = 0;
    // trim trailing spaces
    while (b > 0 && u[b - 1] == ' ') u[--b] = 0;

    if (u[0] == 0) {
        s_copy(u, MAX_URL, DEFAULT_URL);
        b = s_len(u);
    }
    if (starts_with(u, "file://") || u[0] == '/' || u[0] == '.') {
        push_history(u);
        start_load(u);
        return;
    }
    if (!looks_like_url(u)) {
        // Search query: show the query in the address bar, load Bing results.
        char q[MAX_URL];
        url_encode(u, q, MAX_URL);
        char n2[MAX_URL];
        const char* pre = "http://www.bing.com/search?q=";
        int o = 0;
        while (*pre && o < MAX_URL - 1) n2[o++] = *pre++;
        int k = 0;
        while (q[k] && o < MAX_URL - 1) n2[o++] = q[k++];
        const char* suf = "&format=rss";
        k = 0;
        while (suf[k] && o < MAX_URL - 1) n2[o++] = suf[k++];
        n2[o] = 0;
        push_history(n2);
        start_load(n2);
        s_copy(s.url, MAX_URL, u);
        s.url_len = s_len(u);
        return;
    }
    if (!url_is_http(u)) {
        char n2[MAX_URL];
        int o = 0;
        const char* pre = "http://";
        while (*pre && o < MAX_URL - 1) n2[o++] = *pre++;
        int k = 0;
        while (u[k] && o < MAX_URL - 1) n2[o++] = u[k++];
        n2[o] = 0;
        s_copy(u, MAX_URL, n2);
    }
    push_history(u);
    start_load(u);
}

inline void go_back() {
    if (s.history_pos > 0) {
        s.history_pos--;
        start_load(s.history[s.history_pos]);
    }
}

inline void go_forward() {
    if (s.history_pos + 1 < s.history_len) {
        s.history_pos++;
        start_load(s.history[s.history_pos]);
    }
}

inline void reload() {
    if (s.current[0]) start_load(s.current);
}

// ---------------- interaction ----------------
inline void handle_key(int scancode, char ascii) {
    (void)scancode;
    if (ascii == '\b') {
        if (s.url_len > 0) s.url[--s.url_len] = 0;
    } else if (ascii == '\n') {
        navigate(s.url);
    } else if (ascii >= 32 && ascii < 127 && s.url_len < MAX_URL - 1) {
        s.url[s.url_len++] = ascii;
        s.url[s.url_len] = 0;
    }
}

static bool toolbar_hit(gui::Window* w, int mx, int my, int* btn) {
    int x = w->bounds.x;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w;
    if (my < y || my >= y + TOOLBAR_H) return false;
    for (int i = 0; i < 3; i++) {
        int bx = x + 6 + i * 36;
        if (mx >= bx && mx < bx + 30) { *btn = i; return true; }
    }
    int gx = x + ww - 54;
    if (mx >= gx && mx < gx + 46) { *btn = 3; return true; }
    return false;
}

static bool handle_click(gui::Window* w, int mx, int my) {
    int x = w->bounds.x;
    int y = w->bounds.y + 30;
    int wh = w->bounds.h - 30;
    int page_y = y + TOOLBAR_H;
    int page_h = wh - TOOLBAR_H - STATUS_H;
    if (page_h < 8) page_h = 8;

    int btn = -1;
    if (toolbar_hit(w, mx, my, &btn)) {
        if (btn == 0) go_back();
        else if (btn == 1) go_forward();
        else if (btn == 2) reload();
        else navigate(s.url);
        return true;
    }
    if (my >= y && my < y + TOOLBAR_H) {
        s.url_focused = true;
        return true;
    }

    if (my >= page_y && my < page_y + page_h) {
        s.url_focused = false;
        int line = (my - page_y + s.scroll) / LINE_H;
        int rx = mx - (x + 8);
        for (int i = 0; i < s.link_count; i++) {
            if (s.links[i].line == line && rx >= s.links[i].x0 && rx < s.links[i].x1) {
                char resolved[MAX_URL];
                resolve_url(s.current, s.links[i].url, resolved, MAX_URL);
                navigate(resolved);
                return true;
            }
        }
    }
    return true;   // swallow clicks inside the window
}

inline void wheel(int sd) {
    s.scroll -= sd * 30;
    if (s.scroll < 0) s.scroll = 0;
    if (s.scroll > s.max_scroll) s.scroll = s.max_scroll;
}

static bool prev_left = false;

inline void init() {
    s.url[0] = 0;
    s.url_len = 0;
    s.current[0] = 0;
    s.source_len = 0;
    s.plain_len = 0;
    s.raw_count = 0;
    s.layout_len = 0;
    s.line_count = 0;
    s.link_count = 0;
    s.history_pos = -1;
    s.history_len = 0;
    s.scroll = 0;
    s.max_scroll = 0;
    s.wrap_cols = 0;
    s.status[0] = 0;
    s.loading = false;
    s.started = false;
    s.url_focused = true;
    s.hover_btn = -1;
}

static void mouse_update(gui::Window* w, int mx, int my, bool left) {
    if (left && !prev_left) handle_click(w, mx, my);
    prev_left = left;
}

// ---------------- drawing ----------------
static void draw(gui::Window* w) {
    int mx = mouse::cursor_x;
    int my = mouse::cursor_y;
    int x = w->bounds.x;
    int y = w->bounds.y + 30;
    int ww = w->bounds.w;
    int wh = w->bounds.h - 30;
    int page_y = y + TOOLBAR_H;
    int page_h = wh - TOOLBAR_H - STATUS_H;
    if (page_h < 8) page_h = 8;

    if (!s.started) {
        s.started = true;
        s.url[0] = 0;
        s.url_len = 0;
        navigate(DEFAULT_URL);
    }

    int cols = (ww - 16) / 8;
    if (cols < 4) cols = 4;
    if (cols != s.wrap_cols) rewrap(cols);

    s.max_scroll = s.line_count * LINE_H - page_h;
    if (s.max_scroll < 0) s.max_scroll = 0;
    if (s.scroll > s.max_scroll) s.scroll = s.max_scroll;

    // toolbar
    graphics::fill_rect(x, y, ww, TOOLBAR_H, Color(30, 33, 40));
    graphics::fill_rect(x, y + TOOLBAR_H - 1, ww, 1, Color(45, 50, 58));

    const char* btns[4] = {"<", ">", "R", "Go"};
    s.hover_btn = -1;
    for (int i = 0; i < 3; i++) {
        int bx = x + 6 + i * 36;
        bool hov = (mx >= bx && mx < bx + 30 && my >= y && my < y + TOOLBAR_H);
        graphics::fill_rounded_rect(bx, y + 4, 30, TOOLBAR_H - 8, 6,
            hov ? Color(58, 64, 72) : Color(42, 46, 54));
        font::draw_string(bx + 11, y + 9, btns[i], hov ? Color(240, 240, 240) : Color(160, 160, 170));
        if (hov) s.hover_btn = i;
    }
    {
        int gx = x + ww - 54;
        bool hov = (mx >= gx && mx < gx + 46 && my >= y && my < y + TOOLBAR_H);
        graphics::fill_rounded_rect(gx, y + 4, 46, TOOLBAR_H - 8, 6,
            hov ? Color(61, 174, 233) : Color(48, 56, 66));
        font::draw_string(gx + 10, y + 9, "Go", Color(240, 240, 240));
        if (hov) s.hover_btn = 3;
    }

    // address bar
    int ab_x = x + 6 + 3 * 36 + 6;
    int ab_w = ww - (ab_x - x) - 54 - 6;
    if (ab_w < 40) ab_w = 40;
    graphics::fill_rounded_rect(ab_x, y + 4, ab_w, TOOLBAR_H - 8, 6, Color(20, 22, 28));
    graphics::draw_rect(ab_x, y + 4, ab_w, TOOLBAR_H - 8, Color(60, 66, 76), 1);
    s.url_len = s_len(s.url);
    int urll = s.url_len;
    int max_chars = (ab_w - 14) / 8;
    int draw_off = urll - max_chars;
    if (draw_off < 0) draw_off = 0;
    for (int i = draw_off; i < urll; i++) {
        font::draw_char(ab_x + 8 + (i - draw_off) * 8, y + 9, s.url[i],
                        s.loading ? Color(61, 174, 233) : Color(220, 220, 230));
    }
    s.status_tick++;
    if (s.url_focused && (s.status_tick / 30) % 2 == 0) {
        graphics::fill_rect(ab_x + 8 + (urll - draw_off) * 8, y + 9, 2, 16, Color(61, 174, 233));
    }

    // page
    graphics::fill_rect(x, page_y, ww, page_h, Color(16, 16, 24));
    graphics::fill_rect(x, page_y, ww, 1, Color(40, 44, 52));

    int visible = page_h / LINE_H;
    if (visible < 1) visible = 1;
    int start = s.scroll / LINE_H;
    int text_x = x + 8;

    if (s.line_count == 0 && !s.loading) {
        font::draw_string(text_x, page_y + 8, "Empty page", Color(120, 120, 140));
    }

    for (int li = start; li < start + visible && li < s.line_count; li++) {
        int ly = page_y + li * LINE_H - s.scroll;
        if (ly + LINE_H <= page_y) continue;
        if (ly > page_y + page_h) break;
        int lx = text_x;
        for (int k = 0; k < s.line_len[li]; k++) {
            char c = s.layout[s.line_start[li] + k];
            font::draw_char(lx, ly, c, Color(210, 210, 220));
            lx += 8;
        }
        // links (drawn on top of the same line)
        for (int i = 0; i < s.link_count; i++) {
            if (s.links[i].line != li) continue;
            int lx0 = text_x + s.links[i].x0;
            int lx1 = text_x + s.links[i].x1;
            graphics::fill_rect_alpha(lx0, ly, lx1 - lx0, LINE_H, Color(61, 174, 233, 26));
            for (int k = s.links[i].x0 / 8; k < (s.links[i].x1 + 7) / 8; k++) {
                if (k >= s.line_len[li]) break;
                char c = s.layout[s.line_start[li] + k];
                font::draw_char(text_x + k * 8, ly, c, Color(61, 174, 233));
            }
            graphics::fill_rect(lx0, ly + 13, lx1 - lx0, 1, Color(61, 174, 233));
        }
    }

    // loading indicator
    if (s.loading) {
        graphics::fill_rect(x, page_y + 2, ww, 2, Color(30, 33, 40));
        graphics::fill_rect(x, page_y + 2, (ww * (s.status_tick % 60)) / 60, 2, Color(61, 174, 233));
    }

    // scrollbar
    if (s.max_scroll > 0) {
        int sb_x = x + ww - 14;
        int sb_y = page_y + 4;
        int sb_h = page_h - 8;
        graphics::fill_rounded_rect(sb_x, sb_y, 8, sb_h, 4, Color(24, 26, 32));
        int thumb_h = sb_h * (page_h / LINE_H) / s.line_count;
        if (thumb_h < 12) thumb_h = 12;
        if (thumb_h > sb_h) thumb_h = sb_h;
        int range = sb_h - thumb_h;
        int off = s.max_scroll > 0 ? (s.scroll * range / s.max_scroll) : 0;
        graphics::fill_rounded_rect(sb_x, sb_y + off, 8, thumb_h, 4, Color(61, 174, 233));
    }

    // status bar
    int st_y = page_y + page_h;
    graphics::fill_rect(x, st_y, ww, STATUS_H, Color(24, 26, 32));
    graphics::fill_rect(x, st_y, ww, 1, Color(45, 50, 58));
    if (s.loading) {
        font::draw_string(x + 8, st_y + 4, "Loading...", Color(61, 174, 233));
    } else if (s.status[0]) {
        font::draw_string(x + 8, st_y + 4, s.status, Color(150, 152, 162));
    }
    char nb[8];
    int v = s.line_count;
    int ni = 0;
    if (v == 0) nb[ni++] = '0';
    while (v > 0) { nb[ni++] = '0' + v % 10; v /= 10; }
    nb[ni] = 0;
    font::draw_string(x + ww - 8 - (ni * 8) - 40, st_y + 4, nb, Color(110, 112, 122));
    font::draw_string(x + ww - 8 - 32, st_y + 4, " lines", Color(110, 112, 122));
}

} // namespace browser
