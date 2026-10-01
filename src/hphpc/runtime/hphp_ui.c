/* hphp_ui.c — HolyPHP UI 2.0: native Win32 toolkit (themed, owner-drawn).
 *
 * Design goals (see docs/lib-ui.html): the widgets must look like a modern
 * app, not like 1995, and the HolyPHP wrapper on top must stay tiny.
 *
 * How the "modern" part is achieved without a third-party toolkit:
 *   - one global Theme (bg / surface / text / muted / accent / border,
 *     radius, type scale) that every control reads, plus per-control
 *     overrides and per-control "roles" (primary, subtle, ghost, danger,
 *     success, link);
 *   - everything that Win32 draws as a grey box is owner-drawn here:
 *     buttons, checkboxes, cards, sections, list boxes, sliders, progress
 *     bars and combo boxes (including its popup);
 *   - rounded rectangles everywhere, Segoe UI, and real DPI awareness.
 *
 * Two operating modes, unchanged:
 *   - ui_dispatch(ms): pump events for up to ms milliseconds (run mode)
 *   - ui_run_main(): blocking GetMessage loop (build mode)
 *
 * Handles are int64 ids into a runtime-side table; user callbacks are hval
 * closures invoked through hp_call_value. Every callback runs under
 * hp_try_run so a HolyPHP exception inside a handler prints and keeps the
 * app alive instead of unwinding through C.
 */
#include "hphp_rt.h"

#ifdef _WIN32
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32

/* ================= optional Windows APIs (loaded lazily) ================= */
/* uxtheme/dwmapi are resolved with LoadLibrary so the binary still starts on
 * systems where they are missing, and so no import-table dependency is made. */
typedef UINT  (WINAPI *fn_GetDpiForWindow)(HWND);
typedef HANDLE (WINAPI *fn_SetDpiCtx)(HANDLE);
typedef HRESULT (WINAPI *fn_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *fn_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

static fn_GetDpiForWindow       p_GetDpiForWindow;
static fn_SetDpiCtx             p_SetDpiCtx;
static fn_SetWindowTheme        p_SetWindowTheme;
static fn_DwmSetWindowAttribute p_DwmSetWindowAttribute;
static int   g_dpi = 96;
static bool  g_dpi_aware = true;

static void ensure_common(void);   /* needed by hpui_textw, defined further down */

static void load_optional_apis(void) {
    static bool done = false;
    if (done) return;
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    HMODULE dw = LoadLibraryW(L"dwmapi.dll");
    HMODULE us = GetModuleHandleW(L"user32.dll");
    if (ux) p_SetWindowTheme     = (fn_SetWindowTheme)(void *)GetProcAddress(ux, "SetWindowTheme");
    if (dw) p_DwmSetWindowAttribute =
        (fn_DwmSetWindowAttribute)(void *)GetProcAddress(dw, "DwmSetWindowAttribute");
    if (us) {
        p_GetDpiForWindow = (fn_GetDpiForWindow)(void *)GetProcAddress(us, "GetDpiForWindow");
        p_SetDpiCtx = (fn_SetDpiCtx)(void *)GetProcAddress(us, "SetProcessDpiAwarenessContext");
    }
    /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is the pseudo-handle -4.
     * If the call fails the OS is bitmap-virtualising us, so we must NOT
     * scale anything by hand or every coordinate gets scaled twice. */
    if (p_SetDpiCtx) {
        g_dpi_aware = p_SetDpiCtx((HANDLE)(LONG_PTR)-4) != NULL;
        if (!g_dpi_aware) g_dpi = 96;
    } else {
        g_dpi_aware = false;
    }
    if (g_dpi_aware) {
        /* Learn the real scale BEFORE the first window exists, so the window
         * and the widgets inside it agree on what a pixel is worth. */
        HDC dc = GetDC(NULL);
        int lpx = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 0;
        if (dc) ReleaseDC(NULL, dc);
        if (lpx >= 72 && lpx <= 480) g_dpi = lpx;
    }
    done = true;
}

static int sc(int v) { return MulDiv(v, g_dpi, 96); }

/* ================= theme ================= */
typedef struct UiTheme {
    COLORREF bg, surface, elevated, text, muted, accent, on_accent, border;
    int radius;        /* corner radius, px            */
    int font_pt;       /* body type size, points       */
    int title_pt;      /* heading type size, points    */
    int row_h;         /* list / popup row height, px  */
    int pad;           /* default horizontal padding   */
    bool dark;
} UiTheme;

/* roles: 0 default, 1 primary, 2 subtle, 3 ghost, 4 danger, 5 success, 6 link */
#define UI_ROLE_DEFAULT 0
#define UI_ROLE_PRIMARY 1
#define UI_ROLE_SUBTLE  2
#define UI_ROLE_GHOST   3
#define UI_ROLE_DANGER  4
#define UI_ROLE_SUCCESS 5
#define UI_ROLE_LINK    6

static UiTheme g_theme = {
    RGB(0x0F, 0x11, 0x15),   /* bg         */
    RGB(0x17, 0x1A, 0x21),   /* surface    */
    RGB(0x1E, 0x22, 0x2B),   /* elevated   */
    RGB(0xE8, 0xEA, 0xF0),   /* text       */
    RGB(0x96, 0x9F, 0xB0),   /* muted      */
    RGB(0x4C, 0x7D, 0xFF),   /* accent     */
    RGB(0xFF, 0xFF, 0xFF),   /* on_accent  */
    RGB(0x2B, 0x31, 0x3D),   /* border     */
    8, 10, 16, 26, 12, true
};

/* colour maths -------------------------------------------------------- */
static COLORREF mix(COLORREF a, COLORREF b, int pct) {
    int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
    int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
    int t = pct;
    return RGB((ar * (100 - t) + br * t) / 100,
               (ag * (100 - t) + bg * t) / 100,
               (ab * (100 - t) + bb * t) / 100);
}
static COLORREF lighten(COLORREF c, int pct) { return mix(c, RGB(255, 255, 255), pct); }
static COLORREF darken(COLORREF c, int pct)  { return mix(c, RGB(0, 0, 0), pct); }
/* a soft tint of a colour: what a modern UI uses for hover/selected fills */
static COLORREF tint(COLORREF c) {
    return g_theme.dark ? mix(c, g_theme.surface, 35) : mix(c, RGB(255, 255, 255), 88);
}

/* ================= font cache ================= */
typedef struct { int pt; bool bold; HFONT f; } FontEnt;
static FontEnt g_fonts[64];
static int      g_nfonts = 0;

static void fonts_drop(void) {
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].f) DeleteObject(g_fonts[i].f);
    g_nfonts = 0;
}

static HFONT ui_font(int pt, bool bold) {
    if (pt <= 0) pt = g_theme.font_pt;
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].pt == pt && g_fonts[i].bold == bold)
            return g_fonts[i].f;
    if (g_nfonts >= (int)(sizeof g_fonts / sizeof g_fonts[0])) fonts_drop();
    HFONT f = CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0,
                          bold ? FW_SEMIBOLD : FW_NORMAL,
                          FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          L"Segoe UI");
    g_fonts[g_nfonts].pt = pt;
    g_fonts[g_nfonts].bold = bold;
    g_fonts[g_nfonts].f = f;
    g_nfonts++;
    return f;
}

static void fonts_apply(HWND hw) {
    SendMessageW(hw, WM_SETFONT, (WPARAM)ui_font(g_theme.font_pt, false), TRUE);
}

/* ================= handle table ================= */
typedef struct UiEntry {
    int64_t id;
    int     kind;
    HWND    hwnd;
    HMENU   menu_id;             /* menu item command id / popup HMENU */
    hval    cb_click, cb_change, cb_action, cb_key;
    bool    checked_flag;
    /* --- v2 presentation --- */
    int64_t ov_bg, ov_fg, ov_accent, ov_border;   /* -1 = inherit theme */
    int     ov_radius, ov_font_pt;
    bool    ov_font_bold;
    int     role;
    bool    hovered, pressed, focused;
    int     hover_index;
    int     value, min_v, max_v;                 /* slider / progress */
    wchar_t **items;                             /* combo items       */
    int     n_items, cap_items;
    int     sel;                                 /* combo selection   */
    WNDPROC old_proc;                            /* subclassed wndproc*/
    /* --- scrolling (window entry owns the offset) --- */
    int     scroll_y, content_h;
    /* --- logical, unscrolled geometry: scrolling replays these --- */
    int     base_x, base_y, base_w, base_h;
} UiEntry;

static UiEntry *tbl = NULL;
static size_t tbl_len = 0, tbl_cap = 0;
static int64_t next_id = 1;

static UiEntry *tbl_find(int64_t id) {
    for (size_t i = 0; i < tbl_len; i++)
        if (tbl[i].id == id) return &tbl[i];
    return NULL;
}
static UiEntry *entry_of(HWND hw) {
    if (!hw) return NULL;
    int64_t id = (int64_t)GetWindowLongPtrW(hw, GWLP_USERDATA);
    return id ? tbl_find(id) : NULL;
}

static void items_free(UiEntry *e) {
    for (int i = 0; i < e->n_items; i++) free(e->items[i]);
    free(e->items);
    e->items = NULL;
    e->n_items = e->cap_items = 0;
}

static void tbl_add(int64_t id, int kind, HWND hwnd) {
    if (tbl_len == tbl_cap) {
        tbl_cap = tbl_cap ? tbl_cap * 2 : 32;
        tbl = (UiEntry *)realloc(tbl, tbl_cap * sizeof(UiEntry));
    }
    UiEntry *e = &tbl[tbl_len++];
    memset(e, 0, sizeof *e);
    e->id = id;
    e->kind = kind;
    e->hwnd = hwnd;
    e->cb_click = e->cb_change = e->cb_action = e->cb_key = hp_null;
    e->ov_bg = e->ov_fg = e->ov_accent = e->ov_border = -1;
    e->ov_radius = -1;
    e->ov_font_pt = -1;
    e->role = UI_ROLE_DEFAULT;
    e->hover_index = -1;
    e->min_v = 0;
    e->max_v = 100;
    e->sel = -1;
}

/* ================= painting helpers ================= */
static void fill_rect(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* rounded fill + optional 1px outline; radius <= 0 falls back to a plain rect */
static void fill_round(HDC dc, RECT r, int radius, COLORREF fill,
                       COLORREF border, int bwidth) {
    HBRUSH br = fill == (COLORREF)-1 ? NULL : CreateSolidBrush(fill);
    HPEN   pen = (bwidth > 0 && border != (COLORREF)-1)
                     ? CreatePen(PS_SOLID, bwidth, border) : (HPEN)GetStockObject(NULL_PEN);
    HBRUSH ob = (HBRUSH)SelectObject(dc, br ? br : GetStockObject(NULL_BRUSH));
    HPEN   op = (HPEN)SelectObject(dc, pen);
    int rr = radius * 2;
    if (rr > 0) RoundRect(dc, r.left, r.top, r.right, r.bottom, rr, rr);
    else        Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    if (br) DeleteObject(br);
    if (bwidth > 0 && border != (COLORREF)-1) DeleteObject(pen);
}

static void draw_str(HDC dc, RECT r, const wchar_t *s, COLORREF fg, HFONT f,
                     UINT flags) {
    if (!s || !*s) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    HFONT of = (HFONT)SelectObject(dc, f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, of);
}

/* ================= resolved colours per control ================= */
typedef struct UiPaint {
    COLORREF bg, fg, border, accent, track;
    int      radius;
    bool     bold;
} UiPaint;

static int entry_radius(UiEntry *e) {
    if (e->ov_radius >= 0) return e->ov_radius;
    return g_theme.radius;
}
static HFONT entry_font(UiEntry *e, bool role_bold) {
    int pt = e->ov_font_pt > 0 ? e->ov_font_pt : g_theme.font_pt;
    return ui_font(pt, e->ov_font_bold || role_bold);
}

/* Real pixel width of a string in the font it will actually be drawn with.
 * The library used to guess strlen * pt * 0.62, which is fine for average
 * text but leaves "iiii" swimming in padding and "WWWW" clipped -- that is
 * what made button padding look inconsistent from one label to the next. */
int64_t hpui_textw(const char *text, int pt, bool bold) {
    if (!text || !*text) return 0;
    ensure_common();
    wchar_t w[1024];
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, w, 1024);
    if (n <= 1) return 0;
    n -= 1; /* n counted the terminating NUL; the extent API wants the real
             * length. GetTextExtentPoint32W rejects -1 outright (it fails with
             * ERROR_INVALID_PARAMETER), which silently produced garbage
             * widths and hence the lopsided button padding. */
    int64_t guess = (int64_t)strlen(text) * (pt > 0 ? pt : 12) * 13 / 20;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return guess;
    HFONT old = (HFONT)SelectObject(dc, ui_font(pt, bold));
    SIZE sz;
    sz.cx = sz.cy = 0;
    BOOL ok = GetTextExtentPoint32W(dc, w, n, &sz);
    SelectObject(dc, old);
    DeleteDC(dc);
    if (!ok || sz.cx <= 0) return guess;
    return (int64_t)sz.cx;
}
/* HolyPHP colours are 0xRRGGBB integers; Win32 COLORREF is 0x00BBGGRR.
 * Every colour entering the layer from the language goes through here, or
 * the red and blue channels come out swapped. */
static COLORREF rgbv(int64_t v) {
    return RGB((COLORREF)((v >> 16) & 255), (COLORREF)((v >> 8) & 255),
               (COLORREF)(v & 255));
}
static COLORREF ov(COLORREF from, int64_t over) { return over >= 0 ? rgbv(over) : from; }

static void resolve_paint(UiEntry *e, UiPaint *p) {
    p->bg = g_theme.bg;
    p->fg = g_theme.text;
    p->border = g_theme.border;
    p->accent = g_theme.accent;
    p->track = g_theme.border;
    p->radius = entry_radius(e);
    p->bold = false;
    bool on = IsWindowEnabled(e->hwnd);

    switch (e->kind) {
    case HPUI_BUTTON: {
        switch (e->role) {
        case UI_ROLE_PRIMARY:
            p->bg = p->accent; p->fg = g_theme.on_accent;
            p->border = p->accent; p->bold = true; break;
        case UI_ROLE_DANGER:
            p->bg = RGB(0xE5, 0x4B, 0x4B); p->fg = RGB(0xFF, 0xFF, 0xFF);
            p->border = p->bg; p->bold = true; break;
        case UI_ROLE_SUCCESS:
            p->bg = RGB(0x2F, 0xA0, 0x6A); p->fg = RGB(0xFF, 0xFF, 0xFF);
            p->border = p->bg; p->bold = true; break;
        case UI_ROLE_GHOST:
            p->bg = g_theme.bg; p->fg = g_theme.text; p->border = g_theme.bg; break;
        case UI_ROLE_LINK:
            p->bg = g_theme.bg; p->fg = p->accent; p->border = g_theme.bg; break;
        default: /* subtle */
            p->bg = g_theme.surface; p->fg = g_theme.text; p->border = g_theme.border; break;
        }
        if (e->hovered && on) {
            if (e->role == UI_ROLE_GHOST || e->role == UI_ROLE_LINK)
                p->bg = tint(p->fg);
            else if (e->role == UI_ROLE_PRIMARY || e->role == UI_ROLE_DANGER ||
                     e->role == UI_ROLE_SUCCESS)
                p->bg = lighten(p->bg, 10);
            else
                p->bg = g_theme.elevated;
        }
        if (e->pressed && on) p->bg = darken(p->bg, 12);
        break;
    }
    case HPUI_CHECKBOX:
        p->bg = g_theme.bg; p->fg = g_theme.text; p->border = g_theme.border;
        break;
    case HPUI_INPUT:
    case HPUI_COMBO:
        p->bg = g_theme.elevated; p->fg = g_theme.text;
        p->border = e->focused ? p->accent : g_theme.border;
        p->radius = entry_radius(e) > 6 ? entry_radius(e) : 8;
        break;
    case HPUI_LIST:
        p->bg = g_theme.surface; p->fg = g_theme.text; p->border = g_theme.border;
        p->accent = e->focused ? g_theme.accent : g_theme.border;
        p->track = p->bg;
        break;
    case HPUI_PROGRESS:
    case HPUI_SLIDER:
        p->bg = g_theme.bg; p->fg = g_theme.text;
        p->track = g_theme.elevated; p->accent = g_theme.accent;
        break;
    case HPUI_PANEL:
        p->bg = g_theme.surface; p->fg = g_theme.text; p->border = g_theme.border;
        p->radius = entry_radius(e) + 2;
        break;
    case HPUI_GROUP:
        p->bg = g_theme.bg; p->fg = g_theme.muted; p->border = g_theme.border;
        p->bold = true;
        break;
    case HPUI_DIVIDER:
        p->bg = g_theme.bg; p->fg = g_theme.text; p->border = g_theme.border;
        break;
    default: /* LABEL and anything else */
        p->bg = g_theme.bg; p->fg = g_theme.text; p->border = g_theme.bg;
        break;
    }
    if (!on && e->kind != HPUI_LABEL && e->kind != HPUI_GROUP) {
        p->fg = g_theme.muted;
        if (e->kind == HPUI_BUTTON) { p->bg = g_theme.elevated; p->border = g_theme.border; }
    }
    p->bg = ov(p->bg, e->ov_bg);
    p->fg = ov(p->fg, e->ov_fg);
    p->border = ov(p->border, e->ov_border);
    p->accent = ov(p->accent, e->ov_accent);
}

/* ================= callback plumbing ================= */
static void ui_call_guarded(hval cb, hval *args, int nargs);

static void call_cb(hval cb, int64_t arg1, int64_t arg2) {
    if (cb.tag != HV_CLO || !cb.u.c) return;
    hval args[2];
    args[0] = hp_of_int(arg1);
    args[1] = hp_of_int(arg2);
    ui_call_guarded(cb, args, 2);
}

typedef struct CbJob {
    hval cb;
    hval args[2];
    int  nargs;
} CbJob;

static hval cb_body(void *env, hval exv) {
    (void)exv;
    CbJob *j = (CbJob *)env;
    return hp_call_value(j->cb, j->nargs, j->args);
}

static void ui_call_guarded(hval cb, hval *args, int nargs) {
    CbJob j;
    j.cb = cb;
    j.args[0] = args[0];
    j.args[1] = args[1];
    j.nargs = nargs;
    hval r = hp_try_run(cb_body, NULL, &j);
    if (r.tag != HV_NULL) {
        hstr *m = (r.tag == HV_STR) ? r.u.s : hp_val_to_str(r);
        hstr *full = hp_str_concat2(hp_str_lit("[hphp ui] uncaught in event handler: "),
                                    m ? m : hp_str_lit(""));
        fprintf(stderr, "%s\n", full ? full->data : "");
        fflush(stderr);
    }
}

static void fire(UiEntry *e, hval *slot, int64_t a1, int64_t a2) {
    if (e && slot && slot->tag == HV_CLO && slot->u.c) call_cb(*slot, a1, a2);
}

/* repaint every live control of a window: used after a theme change */
static void repaint_window(int64_t win) {
    UiEntry *w = tbl_find(win);
    if (!w || !w->hwnd) return;
    InvalidateRect(w->hwnd, NULL, TRUE);
    for (size_t i = 0; i < tbl_len; i++)
        if (tbl[i].hwnd && IsWindow(tbl[i].hwnd)) InvalidateRect(tbl[i].hwnd, NULL, TRUE);
}

/* ================= init / event loop ================= */
static void ensure_common(void) {
    static bool done = false;
    if (!done) {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof icc;
        icc.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES |
                    ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
        load_optional_apis();
        done = true;
    }
}

void hpui_init(void) { ensure_common(); }

/* internal wakeup message so a parked GetMessage loop returns to the caller */
#define HPUI_WM_WAKE (WM_APP + 0x5150)

static int pump_one(bool block) {
    MSG msg;
    if (block) {
        if (!GetMessageW(&msg, NULL, 0, 0)) return 0;
    } else {
        if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) return 0;
    }
    if (msg.message == HPUI_WM_WAKE) return 1;
    if (msg.message == WM_QUIT) return 0;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    return 1;
}

int64_t hpui_dispatch(int ms) {
    if (ms <= 0) {
        for (;;) {
            MSG msg;
            if (!GetMessageW(&msg, NULL, 0, 0)) return 0;
            if (msg.message == HPUI_WM_WAKE) return 1;
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DWORD start = GetTickCount();
    for (;;) {
        while (pump_one(false)) { }
        DWORD spent = GetTickCount() - start;
        if (spent >= (DWORD)ms) return 1;
        MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(ms - spent), QS_ALLINPUT);
    }
}

void hpui_run_main(void) { while (pump_one(true)) { } }
void hpui_quit(void) { PostQuitMessage(0); }

/* ================= custom control classes ================= */
static LRESULT CALLBACK ui_wndproc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK track_wndproc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK bar_wndproc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK combo_wndproc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK poplist_wndproc(HWND, UINT, WPARAM, LPARAM);

static void apply_dark_decor(HWND hw) {
    if (!p_DwmSetWindowAttribute) return;
    BOOL dark = g_theme.dark ? 1 : 0;
    DWORD attr = 20;                 /* DWMWA_USE_IMMERSIVE_DARK_MODE (22000+) */
    p_DwmSetWindowAttribute(hw, attr, &dark, sizeof dark);
    attr = 19;                       /* same flag on older builds */
    p_DwmSetWindowAttribute(hw, attr, &dark, sizeof dark);
    int pref = 2;                    /* DWMWCP_ROUND */
    attr = 33;                       /* DWMWA_WINDOW_CORNER_PREFERENCE */
    p_DwmSetWindowAttribute(hw, attr, &pref, sizeof pref);
}

static void register_classes(void) {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = ui_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;              /* we erase ourselves */
    wc.lpszClassName = L"HolyPHPWnd";
    RegisterClassW(&wc);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = track_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = L"HolyPHPTrack";
    RegisterClassW(&wc);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = bar_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"HolyPHPBar";
    RegisterClassW(&wc);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = combo_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = L"HolyPHPCombo";
    RegisterClassW(&wc);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = poplist_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    wc.lpszClassName = L"HolyPHPPopList";
    RegisterClassW(&wc);
    done = true;
}

/* ================= window ================= */
int64_t hpui_window_new(const char *title, int x, int y, int w, int h) {
    ensure_common();
    register_classes();
    int sw = sc(w), sh = sc(h);
    if (x == -32000) {   /* sentinel: center on screen */
        x = (GetSystemMetrics(SM_CXSCREEN) - sw) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - sh) / 2;
        if (y < 0) y = 0;
    }
    int64_t id = next_id++;
    tbl_add(id, HPUI_WINDOW, NULL);
    wchar_t wtitle[512];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wtitle, 512);
    HWND hw = CreateWindowExW(0, L"HolyPHPWnd", wtitle,
                              WS_OVERLAPPEDWINDOW,
                              x, y, sw, sh, NULL, NULL, GetModuleHandleW(NULL), NULL);
    UiEntry *e = tbl_find(id);
    if (e) e->hwnd = hw;
    if (hw) {
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)id);
        if (p_GetDpiForWindow) {
            UINT d = p_GetDpiForWindow(hw);
            if (d >= 72 && d <= 480) {
                if (d != (UINT)g_dpi) {
                    g_dpi = (int)d;
                    fonts_drop();
                    /* The frame was sized from the *guessed* DPI. On a monitor
                     * with a different scale that guess is wrong, and every
                     * widget inside is scaled by the real one -- so the window
                     * ends up too small, the flow wraps, and rows overlap.
                     * Re-measure now that we know. */
                    SetWindowPos(hw, NULL, 0, 0, sc(w), sc(h),
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }
        apply_dark_decor(hw);
        ShowWindow(hw, SW_SHOW);
        UpdateWindow(hw);
    }
    return id;
}

int64_t hpui_window_show(int64_t win, bool visible) {
    UiEntry *e = tbl_find(win);
    if (!e || e->kind != HPUI_WINDOW || !e->hwnd) return -1;
    ShowWindow(e->hwnd, visible ? SW_SHOW : SW_HIDE);
    return 0;
}

int64_t hpui_window_title(int64_t win, const char *title) {
    UiEntry *e = tbl_find(win);
    if (!e || !e->hwnd) return -1;
    wchar_t w[512];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, w, 512);
    SetWindowTextW(e->hwnd, w);
    return 0;
}

int64_t hpui_window_size(int64_t win, int w, int h) {
    UiEntry *e = tbl_find(win);
    if (!e || !e->hwnd) return -1;
    SetWindowPos(e->hwnd, NULL, 0, 0, sc(w), sc(h), SWP_NOMOVE | SWP_NOZORDER);
    return 0;
}

int64_t hpui_window_pos(int64_t win, int x, int y) {
    UiEntry *e = tbl_find(win);
    if (!e || !e->hwnd) return -1;
    SetWindowPos(e->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    return 0;
}

int64_t hpui_window_close(int64_t win) {
    UiEntry *e = tbl_find(win);
    if (!e || !e->hwnd) return -1;
    DestroyWindow(e->hwnd);
    e->hwnd = NULL;
    return 0;
}

bool hpui_window_alive(int64_t win) {
    UiEntry *e = tbl_find(win);
    return e && e->hwnd && IsWindow(e->hwnd);
}

/* ================= brush cache ================= */
/* One solid brush per colour. WM_CTLCOLOR* is called on every repaint and
 * allocating a GDI object each time leaks handles fast. */
static HBRUSH brush_for(COLORREF c) {
    static HBRUSH b[16];
    static COLORREF bc[16];
    static int bn = 0;
    for (int i = 0; i < bn; i++)
        if (bc[i] == c) return b[i];
    if (bn == 16) { DeleteObject(b[0]); memmove(b, b + 1, 15 * sizeof(HBRUSH));
                    memmove(bc, bc + 1, 15 * sizeof(COLORREF)); bn = 15; }
    b[bn] = CreateSolidBrush(c);
    bc[bn] = c;
    return b[bn++];
}

/* Clip an edit to the rounded field shape so it can paint an opaque
 * background without losing the rounded corners. */
static void input_region(UiEntry *e) {
    if (!e->hwnd || e->kind != HPUI_INPUT) return;
    RECT rc;
    GetClientRect(e->hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    UiPaint p;
    resolve_paint(e, &p);
    int rr = (p.radius > 0 ? p.radius : 6) * 2;
    if (rr > h) rr = h;
    HRGN r = CreateRoundRectRgn(0, 0, w + 1, h + 1, rr, rr);
    SetWindowRgn(e->hwnd, r, TRUE);   /* system owns r on success */
}

/* ================= scrolling =================
 * A window taller than its content is fine; content taller than the window
 * used to simply run off the bottom with no way to reach it. Every control
 * remembers its unscrolled logical box, so scrolling is just replaying those
 * boxes at an offset -- no re-layout, no second window. */
static void scroll_place(UiEntry *win) {
    if (!win->hwnd) return;
    RECT rc;
    GetClientRect(win->hwnd, &rc);
    int view = rc.bottom - rc.top;
    int max = win->content_h - view;
    if (max < 0) max = 0;
    if (win->scroll_y > max) win->scroll_y = max;
    if (win->scroll_y < 0) win->scroll_y = 0;
    for (size_t i = 0; i < tbl_len; i++) {
        UiEntry *c = &tbl[i];
        if (!c->hwnd || c->hwnd == win->hwnd) continue;
        if (GetParent(c->hwnd) != win->hwnd) continue;
        SetWindowPos(c->hwnd, NULL, sc(c->base_x), sc(c->base_y - win->scroll_y),
                     sc(c->base_w), sc(c->base_h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void scroll_refresh(UiEntry *win) {
    if (!win->hwnd) return;
    int max = 0;
    for (size_t i = 0; i < tbl_len; i++) {
        UiEntry *c = &tbl[i];
        if (!c->hwnd || c->hwnd == win->hwnd) continue;
        if (GetParent(c->hwnd) != win->hwnd) continue;
        int b = c->base_y + c->base_h;
        if (b > max) max = b;
    }
    win->content_h = max;
    scroll_place(win);
}

static void scroll_by(UiEntry *win, int dy) {
    if (!win->hwnd) return;
    int before = win->scroll_y;
    win->scroll_y += dy;
    scroll_place(win);
    if (win->scroll_y != before)
        InvalidateRect(win->hwnd, NULL, FALSE);
}

/* Scroll state, for scripts that want to drive it themselves and for the
 * headless layout test. out_scroll receives the clamped offset, out_content
 * the full content height; either may be NULL. */
int64_t hpui_window_scroll(int64_t h, int dy, int to, int64_t *out_scroll,
                           int64_t *out_content) {
    UiEntry *win = tbl_find(h);
    if (!win || win->kind != HPUI_WINDOW) return -1;
    if (to >= 0) win->scroll_y = to;
    if (dy) scroll_by(win, dy);
    if (!win->hwnd) return -1;
    scroll_place(win);
    RECT rc;
    GetClientRect(win->hwnd, &rc);
    if (out_scroll) *out_scroll = win->scroll_y;
    if (out_content) *out_content = win->content_h;
    return (int64_t)(rc.bottom - rc.top);
}

/* ================= hover / state subclass for native controls ================= */
/* Buttons, checkboxes and edits are real Win32 controls, but they look like
 * Win95 without help, so we subclass them to track hover/press/focus and to
 * paint our own rounded background under the text. */
static void hover_enter(UiEntry *e) {
    if (e->hovered) return;
    e->hovered = true;
    TRACKMOUSEEVENT t;
    memset(&t, 0, sizeof t);
    t.cbSize = sizeof t;
    t.dwFlags = TME_LEAVE;
    t.hwndTrack = e->hwnd;
    t.dwHoverTime = 0;
    TrackMouseEvent(&t);
    InvalidateRect(e->hwnd, NULL, TRUE);
}
static void hover_leave(UiEntry *e) {
    if (!e->hovered) return;
    e->hovered = false;
    e->hover_index = -1;
    InvalidateRect(e->hwnd, NULL, TRUE);
}

static LRESULT CALLBACK hover_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    UiEntry *e = entry_of(hw);
    if (!e) return DefWindowProcW(hw, msg, wp, lp);
    switch (msg) {
    case WM_MOUSEMOVE: hover_enter(e); break;
    case WM_MOUSELEAVE: hover_leave(e); break;
    case WM_LBUTTONDOWN:
        e->pressed = true;
        /* An edit MUST still see the click: swallowing it left the control
         * with no caret, so typing looked dead and Ctrl+A had nothing to
         * select. Track the state, then let the real control have it. */
        if (e->kind == HPUI_INPUT) {
            LRESULT r = CallWindowProcW(e->old_proc, hw, msg, wp, lp);
            InvalidateRect(hw, NULL, TRUE);
            return r;
        }
        InvalidateRect(hw, NULL, TRUE);
        break;
    case WM_LBUTTONUP:
        e->pressed = false;
        if (e->kind == HPUI_INPUT) {
            LRESULT r = CallWindowProcW(e->old_proc, hw, msg, wp, lp);
            InvalidateRect(hw, NULL, TRUE);
            return r;
        }
        InvalidateRect(hw, NULL, TRUE);
        if (e->kind == HPUI_CHECKBOX) {
            e->checked_flag = !e->checked_flag;
            InvalidateRect(hw, NULL, TRUE);
            fire(e, &e->cb_change, e->id, e->checked_flag ? 1 : 0);
        }
        break;
    case WM_CAPTURECHANGED:
        e->pressed = false;
        break;
    case WM_SETFOCUS:  e->focused = true;  InvalidateRect(hw, NULL, TRUE); break;
    case WM_KILLFOCUS: e->focused = false; InvalidateRect(hw, NULL, TRUE); break;
    case WM_GETDLGCODE:
        /* the caret has to move with the arrow keys even though this is not a
         * dialog -- without DLGC_WANTARROWS the parent eats them */
        if (e->kind == HPUI_INPUT)
            return (LRESULT)(DefWindowProcW(hw, msg, wp, lp) | DLGC_WANTARROWS);
        break;
    case WM_KEYDOWN:
        /* Ctrl+A. The stock edit only honours it on some builds, and a
         * subclassed control is exactly the case where it goes missing, so
         * do it here rather than leave select-all silently dead. */
        if (e->kind == HPUI_INPUT && (int)wp == 'A' &&
            (GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            SendMessageW(hw, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_ENABLE:
        InvalidateRect(hw, NULL, TRUE);
        break;
    case WM_ERASEBKGND:
        /* edits paint their own rounded field here; the text that follows is
         * drawn on top of it (opaquely, so nothing ghosts) */
        if (e->kind == HPUI_INPUT) {
            UiPaint p;
            resolve_paint(e, &p);
            RECT rc;
            GetClientRect(hw, &rc);
            input_region(e);
            fill_round((HDC)wp, rc, p.radius, p.bg, p.border, e->focused ? 2 : 1);
            return 1;
        }
        break;
    default: break;
    }
    return CallWindowProcW(e->old_proc, hw, msg, wp, lp);
}

static void subclass(UiEntry *e) {
    LONG_PTR r = SetWindowLongPtrW(e->hwnd, GWLP_WNDPROC, (LONG_PTR)hover_proc);
    if (r) e->old_proc = (WNDPROC)r;
}

static void untheme(HWND hw) {
    /* drop the visual styles so Win32 does not repaint our flat colours */
    if (p_SetWindowTheme) p_SetWindowTheme(hw, L"", L"");
}

/* ================= control creation ================= */
int64_t hpui_ctrl_new(int64_t kind, int64_t parent, const char *text,
                      int x, int y, int w, int h) {
    UiEntry *p = tbl_find(parent);
    if (!p || !p->hwnd) return -1;
    ensure_common();
    register_classes();
    int64_t id = next_id++;
    HWND hw = NULL;
    wchar_t wt[512];
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, wt, 512);
    DWORD base = WS_CHILD | WS_VISIBLE;
    HWND host = p->hwnd;
    int sx = sc(x), sy = sc(y), sw = sc(w), sh = sc(h);

    switch (kind) {
    case HPUI_BUTTON:
        hw = CreateWindowExW(0, L"BUTTON", wt, base | BS_OWNERDRAW,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_LABEL:
        /* owner-draw, so labels get the theme colour, vertical centring and
         * ellipsis behaviour instead of whatever the system font does */
        hw = CreateWindowExW(0, L"STATIC", wt, base | SS_OWNERDRAW,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        untheme(hw);
        break;
    case HPUI_INPUT:
        hw = CreateWindowExW(0, L"EDIT", wt,
                             base | ES_AUTOHSCROLL | ES_LEFT,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        untheme(hw);
        break;
    case HPUI_CHECKBOX:
        hw = CreateWindowExW(0, L"BUTTON", wt, base | BS_OWNERDRAW,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_LIST:
        hw = CreateWindowExW(0, L"LISTBOX", wt,
                             base | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                             LBS_OWNERDRAWFIXED | WS_VSCROLL,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        untheme(hw);
        SendMessageW(hw, LB_SETITEMHEIGHT, 0, (LPARAM)sc(g_theme.row_h));
        break;
    case HPUI_COMBO:
        hw = CreateWindowExW(0, L"HolyPHPCombo", wt, base | WS_TABSTOP,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_PROGRESS:
        hw = CreateWindowExW(0, L"HolyPHPBar", NULL, base,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_SLIDER:
        hw = CreateWindowExW(0, L"HolyPHPTrack", NULL, base | WS_TABSTOP,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_GROUP:
    case HPUI_PANEL:
        hw = CreateWindowExW(0, L"STATIC", wt, base | SS_OWNERDRAW,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        untheme(hw);
        break;
    case HPUI_DIVIDER:
        hw = CreateWindowExW(0, L"STATIC", NULL, base | SS_OWNERDRAW,
                             sx, sy, sw, sh, host, NULL, GetModuleHandleW(NULL), NULL);
        untheme(hw);
        break;
    default:
        return -1;
    }
    if (!hw) return -1;
    tbl_add(id, (int)kind, hw);
    UiEntry *e = tbl_find(id);
    e->base_x = x; e->base_y = y; e->base_w = w; e->base_h = h;
    /* honour whatever the window has already scrolled to */
    if (p && p->scroll_y != 0)
        SetWindowPos(hw, NULL, sx, sc(y - p->scroll_y), sw, sh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)id);
    fonts_apply(hw);
    if (kind == HPUI_BUTTON || kind == HPUI_CHECKBOX || kind == HPUI_INPUT)
        subclass(e);
    if (p_SetWindowTheme) {
        p_SetWindowTheme(hw, g_theme.dark ? L"DarkMode_Explorer" : L"Explorer",
                         g_theme.dark ? NULL : L"");
    }
    /* The very first paint happens inside CreateWindowExW, before tbl_add(),
     * so an owner-drawn control would find no entry and draw nothing. Ask it
     * to paint again now that it is fully registered. */
    InvalidateRect(hw, NULL, FALSE);
    /* a new child changes how far the window has to scroll */
    if (p && p->kind == HPUI_WINDOW) scroll_refresh(p);
    return id;
}

int64_t hpui_ctrl_set_text(int64_t h, const char *text) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, w, 1024);
    if (e->kind == HPUI_COMBO && e->sel >= 0 && e->sel < e->n_items) {
        wcsncpy(e->items[e->sel], w, 1023);
        e->items[e->sel][1023] = 0;
    }
    SetWindowTextW(e->hwnd, w);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_text(int64_t h, hstr **out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    int n = GetWindowTextLengthW(e->hwnd);
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return -1;
    GetWindowTextW(e->hwnd, w, n + 1);
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(need ? (size_t)need : 1);
    if (!u) { free(w); return -1; }
    if (need) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, need, NULL, NULL);
    free(w);
    *out = hp_str_new(u, (size_t)(need > 0 ? need - 1 : 0));
    free(u);
    return 0;
}

int64_t hpui_ctrl_show(int64_t h, bool visible) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    ShowWindow(e->hwnd, visible ? SW_SHOW : SW_HIDE);
    return 0;
}

int64_t hpui_ctrl_enable(int64_t h, bool enabled) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    EnableWindow(e->hwnd, enabled);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_move(int64_t h, int x, int y, int w, int hgt) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    e->base_x = x; e->base_y = y; e->base_w = w; e->base_h = hgt;
    /* the card a Layout resizes may now be taller than the window */
    HWND par = GetParent(e->hwnd);
    if (par) {
        UiEntry *pe = entry_of(par);
        if (pe) scroll_refresh(pe);
    }
    MoveWindow(e->hwnd, sc(x), sc(y), sc(w), sc(hgt), TRUE);
    return 0;
}

/* Where a control is actually painted, in window coordinates: its own
 * rectangle plus every ancestor's offset, minus whatever the window has
 * scrolled. Two siblings that report intersecting rects really are drawn on
 * top of each other, and a control that scrolled off the fold reports a
 * negative y -- which is exactly what a headless layout test needs. */
int64_t hpui_ctrl_rect(int64_t h, int64_t *x, int64_t *y, int64_t *w, int64_t *ht) {
    UiEntry *e = tbl_find(h);
    if (!e) return -1;
    int64_t ax = 0, ay = 0;
    for (UiEntry *c = e; c; ) {
        ax += c->base_x;
        ay += c->base_y;
        UiEntry *par = (c->hwnd && GetParent(c->hwnd)) ? entry_of(GetParent(c->hwnd)) : NULL;
        if (par) ay -= par->scroll_y;
        c = par;
    }
    *x = ax; *y = ay; *w = e->base_w; *ht = e->base_h;
    return 0;
}

int64_t hpui_ctrl_focus(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    SetFocus(e->hwnd);
    return 0;
}

/* ================= list / combo items ================= */
static void combo_add(UiEntry *e, const wchar_t *s) {
    if (e->n_items == e->cap_items) {
        e->cap_items = e->cap_items ? e->cap_items * 2 : 8;
        e->items = (wchar_t **)realloc(e->items, (size_t)e->cap_items * sizeof(wchar_t *));
    }
    size_t n = wcslen(s);
    wchar_t *copy = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    memcpy(copy, s, (n + 1) * sizeof(wchar_t));
    e->items[e->n_items++] = copy;
    if (e->sel < 0 && e->n_items == 1) e->sel = 0;
}

/* Owner-draw one listbox row. LBS_OWNERDRAWFIXED means Windows never holds
 * the text itself, so it is read from the entry's own item array. */
static void draw_list_item(DRAWITEMSTRUCT *di) {
    UiEntry *e = entry_of(di->hwndItem);
    int idx = (int)di->itemID;
    RECT rc = di->rcItem;
    HDC dc = di->hDC;
    if (!e || idx < 0 || idx >= e->n_items) return;
    if (di->itemState & ODS_SELECTED) fill_round(dc, rc, sc(6), tint(g_theme.accent), (COLORREF)-1, 0);
    else if (!(di->itemState & ODS_SELECTED) && (di->itemState & ODS_HOTLIGHT))
        fill_round(dc, rc, sc(6),
                   g_theme.dark ? lighten(g_theme.elevated, 6) : darken(g_theme.elevated, 6),
                   (COLORREF)-1, 0);
    RECT tr = rc;
    tr.left += sc(8);
    tr.right -= sc(6);
    COLORREF fg = (di->itemState & ODS_SELECTED) ? g_theme.on_accent : g_theme.text;
    draw_str(dc, tr, e->items[idx], fg, ui_font(g_theme.font_pt, false),
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

int64_t hpui_list_add(int64_t h, const char *item) {    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    MultiByteToWideChar(CP_UTF8, 0, item ? item : "", -1, w, 1024);
    /* e->items is the single source of truth: LBS_OWNERDRAWFIXED listboxes
     * report no text through LB_GETTEXT, so we must keep our own copy. */
    combo_add(e, w);
    if (e->kind == HPUI_LIST)
        SendMessageW(e->hwnd, LB_ADDSTRING, 0, (LPARAM)w);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return e->n_items - 1;
}

int64_t hpui_list_clear(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    items_free(e);
    e->sel = -1;
    if (e->kind == HPUI_LIST) SendMessageW(e->hwnd, LB_RESETCONTENT, 0, 0);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_list_remove(int64_t h, int64_t index) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (index < 0 || index >= e->n_items) return -1;
    free(e->items[index]);
    for (int i = (int)index; i < e->n_items - 1; i++) e->items[i] = e->items[i + 1];
    e->n_items--;
    if (e->sel >= e->n_items) e->sel = e->n_items - 1;
    if (e->kind == HPUI_LIST) SendMessageW(e->hwnd, LB_DELETESTRING, (WPARAM)index, 0);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_list_count(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = e->n_items;
    return 0;
}

int64_t hpui_list_selected(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (e->kind == HPUI_COMBO) { *out = e->sel; return 0; }
    LRESULT r = SendMessageW(e->hwnd, LB_GETCURSEL, 0, 0);
    *out = (r == LB_ERR) ? -1 : (int64_t)r;
    return 0;
}

int64_t hpui_list_select(int64_t h, int64_t index) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (e->kind == HPUI_COMBO) {
        e->sel = (index >= 0 && index < e->n_items) ? (int)index : -1;
        InvalidateRect(e->hwnd, NULL, TRUE);
        return 0;
    }
    SendMessageW(e->hwnd, LB_SETCURSEL, (WPARAM)index, 0);
    return 0;
}

int64_t hpui_list_text(int64_t h, int64_t index, hstr **out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    /* owner-draw listboxes answer LB_GETTEXT with nothing, so read our copy */
    if (index < 0 || index >= e->n_items) return -1;
    wcsncpy(w, e->items[index], 1023);
    w[1023] = 0;
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(need ? (size_t)need : 1);
    if (!u) return -1;
    if (need) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, need, NULL, NULL);
    *out = hp_str_new(u, (size_t)(need > 0 ? need - 1 : 0));
    free(u);
    return 0;
}

/* ================= checkbox / progress / slider state ================= */
int64_t hpui_check_get(int64_t h, bool *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = e->checked_flag;
    return 0;
}

int64_t hpui_check_set(int64_t h, bool on) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    e->checked_flag = on;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_progress_set(int64_t h, int64_t pct) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    e->value = (int)pct;
    InvalidateRect(e->hwnd, NULL, FALSE);
    return 0;
}

int64_t hpui_progress_get(int64_t h) {
    UiEntry *e = tbl_find(h);
    return e ? e->value : 0;
}

int64_t hpui_slider_get(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = e->value;
    return 0;
}

int64_t hpui_slider_set(int64_t h, int64_t pos) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (pos < e->min_v) pos = e->min_v;
    if (pos > e->max_v) pos = e->max_v;
    e->value = (int)pos;
    InvalidateRect(e->hwnd, NULL, FALSE);
    return 0;
}

int64_t hpui_range_set(int64_t h, int64_t lo, int64_t hi) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (hi <= lo) return -1;
    e->min_v = (int)lo;
    e->max_v = (int)hi;
    if (e->value < e->min_v) e->value = e->min_v;
    if (e->value > e->max_v) e->value = e->max_v;
    InvalidateRect(e->hwnd, NULL, FALSE);
    return 0;
}

int64_t hpui_ctrl_enabled(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return 0;
    return IsWindowEnabled(e->hwnd) ? 1 : 0;
}

int64_t hpui_ctrl_checked(int64_t h) {
    UiEntry *e = tbl_find(h);
    return (e && e->checked_flag) ? 1 : 0;
}

/* ================= colours / fonts / roles ================= */
int64_t hpui_ctrl_set_bg(int64_t h, int64_t rgb) {
    UiEntry *e = tbl_find(h);
    if (!e) return -1;
    if (e->kind == HPUI_WINDOW) { g_theme.bg = rgbv(rgb); repaint_window(h); return 0; }
    e->ov_bg = rgb;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_set_fg(int64_t h, int64_t rgb) {
    UiEntry *e = tbl_find(h);
    if (!e) return -1;
    if (e->kind == HPUI_WINDOW) { g_theme.text = rgbv(rgb); repaint_window(h); return 0; }
    e->ov_fg = rgb;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_accent(int64_t h, int64_t rgb) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    e->ov_accent = rgb;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_radius(int64_t h, int64_t r) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    e->ov_radius = (int)r;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_style(int64_t h, int64_t role) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    e->role = (int)role;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_ctrl_font(int64_t h, int64_t size, bool bold) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (size > 0) e->ov_font_pt = (int)size;
    e->ov_font_bold = bold;
    SendMessageW(e->hwnd, WM_SETFONT,
                 (WPARAM)entry_font(e, false), TRUE);
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

int64_t hpui_picture_load(int64_t h, const char *path) {
    (void)h; (void)path;
    return -1; /* images via GDI+ come later; keep the API surface now */
}

/* ui_theme(win, bg, surface, elevated, text, muted, accent, onAccent, border,
 *         radius, fontPt, titlePt, rowH, dark)
 * Sets the universal look for the window and every control inside it. */
int64_t hpui_theme_set(int64_t win, const int64_t *v, int n) {
    if (n < 14) return -1;
    g_theme.bg        = rgbv(v[0]);
    g_theme.surface   = rgbv(v[1]);
    g_theme.elevated  = rgbv(v[2]);
    g_theme.text      = rgbv(v[3]);
    g_theme.muted     = rgbv(v[4]);
    g_theme.accent    = rgbv(v[5]);
    g_theme.on_accent = rgbv(v[6]);
    g_theme.border    = rgbv(v[7]);
    g_theme.radius    = (int)v[8];
    g_theme.font_pt   = (int)v[9] > 0 ? (int)v[9] : 10;
    g_theme.title_pt  = (int)v[10] > 0 ? (int)v[10] : 16;
    g_theme.row_h     = (int)v[11] > 0 ? (int)v[11] : 26;
    g_theme.pad       = (int)v[12] > 0 ? (int)v[12] : 12;
    g_theme.dark      = v[13] != 0;
    /* every control follows the theme, so clear stale explicit settings */
    for (size_t i = 0; i < tbl_len; i++) {
        if (tbl[i].kind == HPUI_WINDOW) continue;
        tbl[i].ov_bg = tbl[i].ov_fg = tbl[i].ov_accent = tbl[i].ov_border = -1;
        if (tbl[i].hwnd && IsWindow(tbl[i].hwnd))
            fonts_apply(tbl[i].hwnd);
    }
    if (g_theme.row_h > 0) {
        for (size_t i = 0; i < tbl_len; i++)
            if (tbl[i].kind == HPUI_LIST && tbl[i].hwnd)
                SendMessageW(tbl[i].hwnd, LB_SETITEMHEIGHT, 0, (LPARAM)sc(g_theme.row_h));
    }
    UiEntry *w = tbl_find(win);
    if (w && w->hwnd) apply_dark_decor(w->hwnd);
    repaint_window(win);
    return 0;
}

/* ================= menus ================= */
int64_t hpui_menu_new(int64_t win, const char *label) {
    UiEntry *w = tbl_find(win);
    if (!w || !w->hwnd) return -1;
    HMENU menubar = GetMenu(w->hwnd);
    if (!menubar) {
        menubar = CreateMenu();
        SetMenu(w->hwnd, menubar);
    }
    HMENU sub = CreatePopupMenu();
    wchar_t wt[256];
    MultiByteToWideChar(CP_UTF8, 0, label ? label : "", -1, wt, 256);
    int64_t id = next_id++;
    AppendMenuW(menubar, MF_POPUP, (UINT_PTR)sub, wt);
    tbl_add(id, HPUI_MENU, NULL);
    UiEntry *e = tbl_find(id);
    e->hwnd = w->hwnd;
    e->menu_id = sub;
    return id;
}

int64_t hpui_menu_item(int64_t menu, const char *label, int64_t cmdid) {
    UiEntry *m = tbl_find(menu);
    if (!m || !m->menu_id) return -1;
    wchar_t wt[256];
    MultiByteToWideChar(CP_UTF8, 0, label ? label : "", -1, wt, 256);
    int64_t id = next_id++;
    AppendMenuW(m->menu_id, MF_STRING, (UINT_PTR)cmdid, wt);
    tbl_add(id, HPUI_MENUITEM, NULL);
    UiEntry *e = tbl_find(id);
    e->hwnd = m->hwnd;
    e->menu_id = (HMENU)cmdid;
    return id;
}

int64_t hpui_menu_sep(int64_t menu) {
    UiEntry *m = tbl_find(menu);
    if (!m || !m->menu_id) return -1;
    AppendMenuW(m->menu_id, MF_SEPARATOR, 0, NULL);
    return 0;
}

/* ================= events ================= */
int64_t hpui_on_event(int64_t handle, int64_t evtype, hval cb) {
    UiEntry *e = tbl_find(handle);
    if (!e) return -1;
    switch (evtype) {
    case 1: e->cb_click = cb; break;
    case 2: e->cb_change = cb; break;
    case 3: e->cb_action = cb; break;
    case 4: e->cb_key = cb; break;
    default: return -1;
    }
    return 0;
}

int64_t hpui_repaint(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    InvalidateRect(e->hwnd, NULL, TRUE);
    return 0;
}

/* ================= timers ================= */
#define HPUI_TIMER_ID_BASE 1000

int64_t hpui_timer(int64_t ms, hval cb) {
    if (ms <= 0) return -1;
    UiEntry *w = NULL;
    for (size_t i = 0; i < tbl_len; i++)
        if (tbl[i].kind == HPUI_WINDOW && tbl[i].hwnd && IsWindow(tbl[i].hwnd)) { w = &tbl[i]; break; }
    if (!w) return -1;
    static int64_t tid = HPUI_TIMER_ID_BASE;
    int64_t id = tid++;
    tbl_add(id, HPUI_TIMER, NULL);
    UiEntry *e = tbl_find(id);
    e->cb_click = cb;
    SetTimer(w->hwnd, (UINT_PTR)id, (UINT)ms, NULL);
    return id;
}

/* ================= dialogs ================= */
int64_t hpui_msgbox(int64_t win, const char *title, const char *text, int type) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    wchar_t wt[256], tx[2048];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wt, 256);
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, tx, 2048);
    UINT flags;
    switch (type) {
    case 1:  flags = MB_ICONWARNING | MB_OK; break;
    case 2:  flags = MB_ICONERROR | MB_OK; break;
    case 3:  flags = MB_ICONQUESTION | MB_YESNO; break;
    default: flags = MB_ICONINFORMATION | MB_OK; break;
    }
    int r = MessageBoxW(owner, tx, wt, flags);
    if (type == 3) return (r == IDYES) ? 1 : 0;
    return (r == IDOK || r == IDYES) ? 1 : 0;
}

static bool pick_path(HWND owner, bool save, const char *filter, char *out, int outsz) {
    wchar_t fname[1024];
    fname[0] = 0;
    wchar_t wfilter[1024];
    const char *f = filter ? filter : "All files|*.*";
    size_t j = 0;
    for (size_t i = 0; f[i] && j < 1022; i++)
        wfilter[j++] = f[i] == '|' ? 0 : (wchar_t)(unsigned char)f[i];
    wfilter[j] = 0; wfilter[j + 1] = 0;
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = wfilter;
    ofn.lpstrFile = fname;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return false;
    WideCharToMultiByte(CP_UTF8, 0, fname, -1, out, outsz, NULL, NULL);
    return true;
}

hval hpui_open_file(int64_t win, const char *filter) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    char buf[1024];
    if (!pick_path(owner, false, filter, buf, sizeof buf)) return hp_null;
    return hp_of_str(hp_str_new(buf, strlen(buf)));
}

hval hpui_save_file(int64_t win, const char *filter) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    char buf[1024];
    if (!pick_path(owner, true, filter, buf, sizeof buf)) return hp_null;
    return hp_of_str(hp_str_new(buf, strlen(buf)));
}

hval hpui_pick_folder(int64_t win) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    wchar_t path[MAX_PATH];
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose a folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return hp_null;
    bool ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok) return hp_null;
    char u8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, u8, sizeof u8, NULL, NULL);
    return hp_of_str(hp_str_new(u8, strlen(u8)));
}

hval hpui_color_pick(int64_t win, int64_t init_rgb) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    CHOOSECOLORW cc;
    static COLORREF custom[16];
    memset(&cc, 0, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = owner;
    cc.rgbResult = RGB((init_rgb >> 16) & 0xff, (init_rgb >> 8) & 0xff, init_rgb & 0xff);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return hp_null;
    COLORREF c = cc.rgbResult;
    return hp_of_int(((int64_t)GetRValue(c) << 16) | ((int64_t)GetGValue(c) << 8) |
                     (int64_t)GetBValue(c));
}

/* ================= clipboard ================= */
int64_t ui_clip_set(const char *text) {
    if (!OpenClipboard(NULL)) return -1;
    EmptyClipboard();
    size_t n = strlen(text) + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!g) { CloseClipboard(); return -1; }
    memcpy(GlobalLock(g), text, n);
    GlobalUnlock(g);
    SetClipboardData(CF_TEXT, g);
    CloseClipboard();
    return 0;
}

hval ui_clip_get(void) {
    if (!OpenClipboard(NULL)) return hp_null;
    HANDLE h = GetClipboardData(CF_TEXT);
    if (!h) { CloseClipboard(); return hp_null; }
    const char *s = (const char *)GlobalLock(h);
    hval r = s ? hp_of_str(hp_str_new(s, strlen(s))) : hp_null;
    GlobalUnlock(h);
    CloseClipboard();
    return r;
}

/* ================= owner drawing ================= */
static void draw_check(HDC dc, int cx, int cy, int r, COLORREF color, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN op = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, cx - r / 2, cy, NULL);
    LineTo(dc, cx - r / 6, cy + r / 2);
    LineTo(dc, cx + r / 2, cy - r / 2);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void draw_chevron(HDC dc, int cx, int cy, int r, COLORREF color, int width,
                         bool down) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN op = (HPEN)SelectObject(dc, pen);
    if (down) {
        MoveToEx(dc, cx - r, cy - r / 2, NULL);
        LineTo(dc, cx, cy + r / 2);
        LineTo(dc, cx + r, cy - r / 2);
    } else {
        MoveToEx(dc, cx - r, cy + r / 2, NULL);
        LineTo(dc, cx, cy - r / 2);
        LineTo(dc, cx + r, cy + r / 2);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

/* Draws one control into `dc`. `where` is the rectangle in *parent*
 * coordinates (what WM_DRAWITEM supplies); pass NULL when painting the
 * control's own surface, where the client rect is the right frame. */
static void draw_control_at(HWND hw, HDC dc, const RECT *where) {
    UiEntry *e = entry_of(hw);
    if (!e) return;
    UiPaint p;
    resolve_paint(e, &p);
    RECT rc;
    if (where) {
        rc = *where;
        /* shift the viewport so the rest of this function can keep working
         * in the control's own 0,0-based coordinates */
        SetViewportOrgEx(dc, -rc.left, -rc.top, NULL);
    } else {
        GetClientRect(hw, &rc);
    }
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int font_pt = e->ov_font_pt > 0 ? e->ov_font_pt : g_theme.font_pt;
    /* owner-draw controls are never erased for us; clear the item first so a
     * repaint after a font or theme change cannot leave a ghost behind.
     * Labels are exempt: they sit on whatever their container drew. */
    if (where && e->kind != HPUI_LABEL) fill_rect(dc, rc, p.bg);

    switch (e->kind) {
    case HPUI_LABEL: {
        wchar_t buf[1024];
        GetWindowTextW(hw, buf, 1024);
        RECT tr = rc;
        draw_str(dc, tr, buf, p.fg, entry_font(e, false),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case HPUI_BUTTON: {
        fill_round(dc, rc, p.radius, p.bg, p.border, 1);
        wchar_t buf[512];
        GetWindowTextW(hw, buf, 512);
        RECT tr = rc;
        int in = sc(e->role == UI_ROLE_LINK ? 0 : g_theme.pad);
        if (e->role == UI_ROLE_LINK) {
            tr.left = rc.left; tr.right = rc.right;
        } else {
            tr.left += in;
        }
        draw_str(dc, tr, buf, p.fg, entry_font(e, p.bold),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case HPUI_CHECKBOX: {
        wchar_t buf[512];
        GetWindowTextW(hw, buf, 512);
        int box = sc(18);
        int cy = h / 2;
        RECT br = { rc.left, cy - box / 2, rc.left + box, cy + box / 2 };
        COLORREF fill = e->checked_flag ? p.accent : g_theme.elevated;
        COLORREF line = e->checked_flag ? p.accent : (e->hovered ? g_theme.muted : g_theme.border);
        fill_round(dc, br, sc(5), fill, line, 1);
        if (e->checked_flag)
            draw_check(dc, (br.left + br.right) / 2, cy, sc(9),
                       g_theme.on_accent, sc(2));
        RECT tr = { rc.left + box + sc(10), rc.top, rc.right, rc.bottom };
        draw_str(dc, tr, buf, p.fg, entry_font(e, false),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case HPUI_PANEL: {
        RECT r2 = rc;
        fill_round(dc, r2, p.radius, p.bg, p.border, 1);
        break;
    }
    case HPUI_GROUP: {
        wchar_t buf[512];
        GetWindowTextW(hw, buf, 512);
        int txt_w = 0;
        if (buf[0]) {
            HFONT f = entry_font(e, true);
            HDC mem = CreateCompatibleDC(dc);
            HFONT of = (HFONT)SelectObject(mem, f);
            RECT t = { 0, 0, 0, 0 };
            DrawTextW(mem, buf, -1, &t, DT_CALCRECT | DT_SINGLELINE);
            txt_w = t.right - t.left;
            SelectObject(mem, of);
            DeleteDC(mem);
        }
        int cap_y = sc(10);
        RECT line = { rc.left, cap_y, rc.right, cap_y + 1 };
        /* outline with a gap for the caption */
        fill_round(dc, rc, p.radius, (COLORREF)-1, p.border, 1);
        if (txt_w > 0) {
            RECT gap = { rc.left + sc(12), cap_y - 1, rc.left + sc(12) + txt_w + sc(6), cap_y + 2 };
            fill_rect(dc, gap, g_theme.bg);
            RECT tr = { rc.left + sc(12), rc.top, rc.left + sc(18) + txt_w, rc.bottom };
            draw_str(dc, tr, buf, p.fg, entry_font(e, true),
                     DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        (void)line;
        break;
    }
    case HPUI_DIVIDER: {
        int y = h / 2;
        RECT line = { rc.left, y, rc.right, y + 1 };
        fill_rect(dc, line, p.border);
        break;
    }
    case HPUI_LIST: {
        int row = sc(g_theme.row_h);
        if (row <= 0) row = 1;
        int sel = (int)SendMessageW(hw, LB_GETCURSEL, 0, 0);
        int top = (int)SendMessageW(hw, LB_GETTOPINDEX, 0, 0);
        if (sel == LB_ERR) sel = -1;
        fill_rect(dc, rc, p.bg);
        int visible = (h + row - 1) / row;
        for (int i = 0; i < visible; i++) {
            int idx = top + i;
            wchar_t buf[1024];
            LRESULT len = SendMessageW(hw, LB_GETTEXTLEN, (WPARAM)idx, 0);
            if (len == LB_ERR || len >= 1024) continue;
            SendMessageW(hw, LB_GETTEXT, (WPARAM)idx, (LPARAM)buf);
            RECT ir = { rc.left + sc(2), i * row, rc.right - sc(2), (i + 1) * row };
            if (idx == sel)      fill_round(dc, ir, sc(6), tint(p.accent), (COLORREF)-1, 0);
            else if (idx == e->hover_index)
                                     fill_round(dc, ir, sc(6),
                                                g_theme.dark ? lighten(g_theme.surface, 6)
                                                             : darken(g_theme.surface, 5),
                                                (COLORREF)-1, 0);
            RECT tr = ir;
            tr.left += sc(8);
            draw_str(dc, tr, buf, idx == sel ? g_theme.text : p.fg,
                     entry_font(e, false), DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                     DT_END_ELLIPSIS);
        }
        if (e->focused) {
            RECT fr = { 0, 0, w - 1, h - 1 };
            fill_round(dc, fr, p.radius, (COLORREF)-1, p.accent, 1);
        }
        break;
    }
    case HPUI_PROGRESS: {
        int bar = sc(8);
        if (bar > h) bar = h;
        int y = (h - bar) / 2;
        RECT tr = { 0, y, w, y + bar };
        fill_round(dc, tr, bar / 2, g_theme.dark ? lighten(g_theme.elevated, 4) : g_theme.border,
                   (COLORREF)-1, 0);
        int fw = (int)((int64_t)w * e->value / 100);
        if (fw > 0) {
            RECT fr = { 0, y, fw, y + bar };
            fill_round(dc, fr, bar / 2, p.accent, (COLORREF)-1, 0);
        }
        break;
    }
    case HPUI_SLIDER: {
        int bar = sc(6);
        int cy = h / 2;
        RECT tr = { 0, cy - bar / 2, w, cy + bar / 2 };
        fill_round(dc, tr, bar / 2, g_theme.dark ? lighten(g_theme.elevated, 6) : g_theme.border,
                   (COLORREF)-1, 0);
        int thumb = sc(18);
        int range = w - thumb;
        int span = e->max_v - e->min_v;
        int pos = 0;
        if (span > 0) pos = range - (int)(((int64_t)(e->max_v - e->value) * range) / span);
        if (pos < 0) pos = 0;
        if (pos > range) pos = range;
        if (range > 0) {
            RECT fr = { 0, cy - bar / 2, pos + thumb / 2, cy + bar / 2 };
            fill_round(dc, fr, bar / 2, p.accent, (COLORREF)-1, 0);
        }
        RECT cr = { pos, cy - thumb / 2, pos + thumb, cy + thumb / 2 };
        COLORREF cfill = g_theme.dark ? lighten(g_theme.text, 4) : RGB(0xFF, 0xFF, 0xFF);
        if (e->focused) {
            RECT ring = { cr.left - sc(3), cr.top - sc(3), cr.right + sc(3), cr.bottom + sc(3) };
            fill_round(dc, ring, sc(12), tint(p.accent), (COLORREF)-1, 0);
        }
        fill_round(dc, cr, sc(9), cfill, (COLORREF)-1, 0);
        break;
    }
    case HPUI_COMBO: {
        fill_round(dc, rc, p.radius, p.bg, p.border, e->focused ? 2 : 1);
        wchar_t buf[1024];
        buf[0] = 0;
        if (e->sel >= 0 && e->sel < e->n_items) wcsncpy(buf, e->items[e->sel], 1023);
        buf[1023] = 0;
        RECT tr = { rc.left + sc(12), rc.top, rc.right - sc(28), rc.bottom };
        draw_str(dc, tr, buf, e->sel >= 0 ? p.fg : g_theme.muted, entry_font(e, false),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        int cx = rc.right - sc(16), cy = rc.top + h / 2;
        draw_chevron(dc, cx, cy, sc(4), g_theme.muted, sc(2), true);
        break;
    }
    default:
        break;
    }
    (void)font_pt;
}

/* ================= combo dropdown popup ================= */
static HWND       g_popup = NULL;
static int64_t    g_popup_owner = 0;

static void popup_close(void) {
    if (!g_popup) return;
    HWND p = g_popup;
    g_popup = NULL;
    g_popup_owner = 0;
    DestroyWindow(p);
}

static void combo_choose(int idx) {
    UiEntry *e = tbl_find(g_popup_owner);
    if (!e) return;
    if (idx >= 0 && idx < e->n_items && idx != e->sel) {
        e->sel = idx;
        InvalidateRect(e->hwnd, NULL, TRUE);
        fire(e, &e->cb_change, e->id, idx);
    }
}

static void combo_open(HWND combo) {
    UiEntry *e = entry_of(combo);
    if (!e || e->n_items == 0) return;
    popup_close();
    g_popup_owner = e->id;
    RECT wr;
    GetWindowRect(combo, &wr);
    int row = sc(g_theme.row_h);
    int pad = sc(6);
    int hgt = e->n_items * row + pad * 2;
    int maxh = GetSystemMetrics(SM_CYSCREEN) / 2;
    if (hgt > maxh) hgt = maxh;
    int x = wr.left, y = wr.bottom + sc(4);
    if (y + hgt > GetSystemMetrics(SM_CYSCREEN)) {
        y = wr.top - hgt - sc(4);
        if (y < 0) y = 0;
    }
    HWND owner = GetParent(combo);
    g_popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"HolyPHPPopList", L"",
                              WS_POPUP, x, y, wr.right - wr.left, hgt,
                              owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_popup) { g_popup = NULL; g_popup_owner = 0; return; }
    int r = sc(g_theme.radius);
    HRGN reg = CreateRoundRectRgn(0, 0, (wr.right - wr.left) + 1, hgt + 1, r * 2, r * 2);
    SetWindowRgn(g_popup, reg, TRUE);
    if (p_SetWindowTheme) p_SetWindowTheme(g_popup, L"", L"");
    ShowWindow(g_popup, SW_SHOWNA);
    SetCapture(g_popup);
    InvalidateRect(g_popup, NULL, FALSE);
}

static LRESULT CALLBACK poplist_wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    UiEntry *e = tbl_find(g_popup_owner);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        RECT rc;
        GetClientRect(hw, &rc);
        fill_round(dc, rc, sc(g_theme.radius), g_theme.elevated, g_theme.border, 1);
        if (e) {
            int row = sc(g_theme.row_h);
            int pad = sc(6);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hw, &pt);
            for (int i = 0; i < e->n_items; i++) {
                RECT ir = { sc(4), pad + i * row, rc.right - sc(4), pad + (i + 1) * row };
                bool over = pt.y >= ir.top && pt.y < ir.bottom;
                if (i == e->sel)      fill_round(dc, ir, sc(6), tint(g_theme.accent), (COLORREF)-1, 0);
                else if (over)        fill_round(dc, ir, sc(6),
                                                g_theme.dark ? lighten(g_theme.elevated, 6)
                                                             : darken(g_theme.elevated, 6),
                                                (COLORREF)-1, 0);
                RECT tr = ir;
                tr.left += sc(8);
                draw_str(dc, tr, e->items[i], g_theme.text, ui_font(g_theme.font_pt, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        InvalidateRect(hw, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int row = sc(g_theme.row_h), pad = sc(6);
        int idx = (GET_Y_LPARAM(lp) - pad) / row;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc;
        GetClientRect(hw, &rc);
        bool inside = pt.x >= 0 && pt.x < rc.right && pt.y >= 0 && pt.y < rc.bottom;
        if (inside && idx >= 0 && e && idx < e->n_items) combo_choose(idx);
        if (inside) { if (e) SetFocus(e->hwnd); }
        ReleaseCapture();
        popup_close();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { ReleaseCapture(); popup_close(); return 0; }
        break;
    case WM_CAPTURECHANGED:
        popup_close();
        return 0;
    case WM_KILLFOCUS:
        popup_close();
        return 0;
    default: break;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ================= custom slider ================= */
static void track_set_from_x(UiEntry *e, int x) {
    RECT rc;
    GetClientRect(e->hwnd, &rc);
    int thumb = sc(18);
    int range = rc.right - thumb;
    if (range <= 0) return;
    int span = e->max_v - e->min_v;
    if (span <= 0) return;
    int v = (int)(((int64_t)x * span) / range) + e->min_v;
    if (v < e->min_v) v = e->min_v;
    if (v > e->max_v) v = e->max_v;
    if (v == e->value) return;
    e->value = v;
    InvalidateRect(e->hwnd, NULL, FALSE);
    fire(e, &e->cb_change, e->id, v);
}

static LRESULT CALLBACK track_wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    UiEntry *e = entry_of(hw);
    if (!e) return DefWindowProcW(hw, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        draw_control_at(hw, dc, NULL);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hw);
        SetCapture(hw);
        e->pressed = true;
        track_set_from_x(e, GET_X_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        if (e->pressed) { e->pressed = false; ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        e->pressed = false;
        return 0;
    case WM_MOUSEMOVE:
        if (e->pressed) track_set_from_x(e, GET_X_LPARAM(lp));
        return 0;
    case WM_KEYDOWN: {
        int step = 1, v = e->value;
        RECT rc;
        GetClientRect(hw, &rc);
        int page = (rc.right - sc(18)) / 10;
        if (page < 1) page = 1;
        if (wp == VK_LEFT)  v -= step;
        else if (wp == VK_RIGHT) v += step;
        else if (wp == VK_UP)    v += step;
        else if (wp == VK_DOWN)  v -= step;
        else if (wp == VK_PRIOR)   v += page;
        else if (wp == VK_NEXT)    v -= page;
        else if (wp == VK_HOME) v = e->min_v;
        else if (wp == VK_END)  v = e->max_v;
        else return DefWindowProcW(hw, msg, wp, lp);
        if (v < e->min_v) v = e->min_v;
        if (v > e->max_v) v = e->max_v;
        if (v != e->value) {
            e->value = v;
            InvalidateRect(hw, NULL, FALSE);
            fire(e, &e->cb_change, e->id, v);
        }
        return 0;
    }
    case WM_SETFOCUS:  e->focused = true;  InvalidateRect(hw, NULL, FALSE); return 0;
    case WM_KILLFOCUS: e->focused = false; InvalidateRect(hw, NULL, FALSE); return 0;
    default: break;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ================= custom progress bar ================= */
static LRESULT CALLBACK bar_wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        draw_control_at(hw, dc, NULL);
        EndPaint(hw, &ps);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ================= custom combo box ================= */
static LRESULT CALLBACK combo_wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    UiEntry *e = entry_of(hw);
    if (!e) return DefWindowProcW(hw, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        draw_control_at(hw, dc, NULL);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        SetFocus(hw);
        combo_open(hw);
        return 0;
    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE && g_popup) { ReleaseCapture(); popup_close(); return 0; }
        if (wp == VK_DOWN || wp == VK_UP || wp == VK_SPACE || wp == VK_RETURN) {
            combo_open(hw);
            return 0;
        }
        if (e->n_items == 0) return 0;
        int s = e->sel;
        if (wp == VK_DOWN) s += 1;
        else if (wp == VK_UP) s -= 1;
        else return DefWindowProcW(hw, msg, wp, lp);
        if (s < 0) s = e->n_items - 1;
        if (s >= e->n_items) s = 0;
        if (s != e->sel) {
            e->sel = s;
            InvalidateRect(hw, NULL, TRUE);
            fire(e, &e->cb_change, e->id, s);
        }
        return 0;
    }
    case WM_SETFOCUS:  e->focused = true;  InvalidateRect(hw, NULL, TRUE); return 0;
    case WM_KILLFOCUS: e->focused = false; InvalidateRect(hw, NULL, TRUE); return 0;
    case WM_DESTROY:   if (g_popup_owner == e->id) popup_close(); break;
    default: break;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ================= window proc: routes events ================= */
static LRESULT CALLBACK ui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int64_t id = (int64_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    UiEntry *e = id ? tbl_find(id) : NULL;

    switch (msg) {
    case HPUI_WM_WAKE:
        return 0;

    /* Window-level shortcuts. Escape closes; anything the script binds
     * through $app->onKey() is delivered as (vk, character). */
    case WM_KEYDOWN: {
        UiEntry *we = entry_of(hwnd);
        int vk = (int)wp;
        if (vk == VK_ESCAPE && we && we->kind == HPUI_WINDOW)
            DestroyWindow(hwnd);
        /* PageUp/Down and Ctrl+Home/End scroll a window whose content does not
         * fit. The arrow keys are deliberately left alone: a script that binds
         * them through onKey() (the gallery cycles themes with Left/Right)
         * must keep getting them. */
        if (we && we->kind == HPUI_WINDOW) {
            RECT rc; GetClientRect(hwnd, &rc);
            int page = rc.bottom - rc.top - sc(40);
            if (page < 1) page = 1;
            if (vk == VK_PRIOR) scroll_by(we, -page);
            else if (vk == VK_NEXT) scroll_by(we, page);
            else if (vk == VK_HOME && GetKeyState(VK_CONTROL) < 0) scroll_by(we, -we->content_h);
            else if (vk == VK_END && GetKeyState(VK_CONTROL) < 0) scroll_by(we, we->content_h);
        }
        if (we && we->cb_key.tag == HV_CLO && we->cb_key.u.c) {
            /* WPARAM is the *virtual key*, not the character. Casting it to
             * wchar gave '[' as VK_OEM_4 (0xDB), so every arrow-key and
             * punctuation binding in a script silently never fired.
             * Translate through the keyboard layout instead. */
            BYTE state[256];
            GetKeyboardState(state);
            wchar_t buf[8];
            int n = ToUnicode(vk, (int)(lp >> 16), state, buf, 8, 0);
            int64_t ch = (n > 0) ? (int64_t)buf[0] : 0;
            if (ch == VK_RETURN) ch = '\r';
            if (ch == VK_BACK)   ch = '\b';
            fire(we, &we->cb_key, vk, ch);
        }
        if (!we || we->cb_key.tag != HV_CLO || !we->cb_key.u.c)
            return DefWindowProcW(hwnd, msg, wp, lp);
        return 0;
    }

    /* Wheel scrolling: content taller than the window used to be unreachable. */
    case WM_MOUSEWHEEL: {
        UiEntry *we = entry_of(hwnd);
        if (!we || we->kind != HPUI_WINDOW) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int lines = 3;
        SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines <= 0) lines = 3;
        int step = (abs(delta) / WHEEL_DELTA) * lines * sc(g_theme.row_h);
        scroll_by(we, delta > 0 ? step : -step);
        return 0;
    }

    case WM_SIZE: {
        UiEntry *we = entry_of(hwnd);
        if (we && we->kind == HPUI_WINDOW) scroll_place(we);
        break;
    }

    case WM_COMMAND: {
        int code = HIWORD(wp);
        HWND src = (HWND)lp;
        int64_t sid = src ? (int64_t)GetWindowLongPtrW(src, GWLP_USERDATA) : 0;
        UiEntry *se = sid ? tbl_find(sid) : NULL;
        if (se) {
            if (code == BN_CLICKED && se->kind == HPUI_BUTTON) {
                fire(se, &se->cb_click, sid, 0);
                return 0;
            }
            if ((code == EN_CHANGE && se->kind == HPUI_INPUT) ||
                (code == LBN_SELCHANGE && se->kind == HPUI_LIST)) {
                if (se->kind == HPUI_LIST) {
                    LRESULT r = SendMessageW(se->hwnd, LB_GETCURSEL, 0, 0);
                    se->sel = (r == LB_ERR) ? -1 : (int)r;
                }
                fire(se, &se->cb_change, sid, 0);
                return 0;
            }
        }
        for (size_t i = 0; i < tbl_len; i++)
            if (tbl[i].kind == HPUI_MENUITEM && tbl[i].menu_id == (HMENU)wp) {
                fire(&tbl[i], &tbl[i].cb_click, tbl[i].id, (int64_t)wp);
                return 0;
            }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_BUTTON || di->CtlType == ODT_STATIC) {
            draw_control_at(di->hwndItem, di->hDC, &di->rcItem);
            return TRUE;
        }
        if (di->CtlType == ODT_LISTBOX) {
            draw_list_item(di);
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_TIMER:
        if (wp >= HPUI_TIMER_ID_BASE) {
            UiEntry *t = tbl_find((int64_t)wp);
            if (t) fire(t, &t->cb_click, (int64_t)wp, 0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HWND src = (HWND)lp;
        UiEntry *se = src ? entry_of(src) : NULL;
        UiPaint p;
        if (se) resolve_paint(se, &p);
        else { p.fg = g_theme.text; p.bg = g_theme.bg; p.border = g_theme.border; p.radius = 0; }
        SetTextColor((HDC)wp, p.fg);
        /* An edit must own its background. With a NULL_BRUSH it never clears
         * the pixels it stops covering -- scrolling the caret or shrinking a
         * selection smears the old glyphs into the new text. The rounded field
         * is kept by clipping the control to a rounded region (input_region),
         * so an opaque brush no longer squares off the corners. */
        if (se && se->kind == HPUI_INPUT) {
            SetBkColor((HDC)wp, p.bg);
            SetBkMode((HDC)wp, OPAQUE);
            return (LRESULT)brush_for(p.bg);
        }
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLORLISTBOX: {
        UiPaint p;
        p.bg = g_theme.surface; p.fg = g_theme.text;
        SetBkColor((HDC)wp, p.bg);
        SetTextColor((HDC)wp, p.fg);
        static HBRUSH br = NULL;
        static COLORREF br_c = 0;
        if (br && br_c != p.bg) { DeleteObject(br); br = NULL; }
        if (!br) { br = CreateSolidBrush(p.bg); br_c = p.bg; }
        return (LRESULT)br;
    }
    case WM_CTLCOLORBTN: {
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        fill_rect((HDC)wp, rc, g_theme.bg);
        return 1;
    }

    case WM_DPICHANGED: {
        int nd = LOWORD(lp);
        if (!g_dpi_aware) return DefWindowProcW(hwnd, msg, wp, lp);
        if (nd >= 72 && nd <= 480 && nd != g_dpi) {
            g_dpi = nd;
            fonts_drop();
            for (size_t i = 0; i < tbl_len; i++)
                if (tbl[i].hwnd && IsWindow(tbl[i].hwnd)) fonts_apply(tbl[i].hwnd);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) popup_close();
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CLOSE:
        popup_close();
        if (e) fire(e, &e->cb_action, id, 0);
        if (e && e->cb_action.tag == HV_CLO) return 0;   /* handler decides */
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        popup_close();
        int windows_left = 0;
        for (size_t i = 0; i < tbl_len; i++)
            if (tbl[i].kind == HPUI_WINDOW && tbl[i].hwnd &&
                tbl[i].id != id && IsWindow(tbl[i].hwnd))
                windows_left++;
        if (e) { e->hwnd = NULL; }
        if (windows_left == 0) PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

#endif /* _WIN32 */

/* ================= non-Windows stubs (API-complete, inert) ================= */
#ifndef _WIN32
void hpui_init(void) {}
int64_t hpui_dispatch(int ms) { (void)ms; return 0; }
void hpui_run_main(void) {}
void hpui_quit(void) {}
int64_t hpui_window_new(const char *t, int x, int y, int w, int h) {
    (void)t; (void)x; (void)y; (void)w; (void)h; return -1;
}
int64_t hpui_window_show(int64_t w, bool v) { (void)w; (void)v; return -1; }
int64_t hpui_window_title(int64_t w, const char *t) { (void)w; (void)t; return -1; }
int64_t hpui_window_size(int64_t w, int a, int b) { (void)w; (void)a; (void)b; return -1; }
int64_t hpui_window_pos(int64_t w, int a, int b) { (void)w; (void)a; (void)b; return -1; }
int64_t hpui_window_close(int64_t w) { (void)w; return -1; }
int64_t hpui_window_scroll(int64_t w, int a, int b, int64_t *s, int64_t *c) {
    (void)w; (void)a; (void)b; if (s) *s = 0; if (c) *c = 0; return -1;
}
bool hpui_window_alive(int64_t w) { (void)w; return false; }
int64_t hpui_ctrl_new(int64_t k, int64_t p, const char *t, int x, int y, int w, int h) {
    (void)k; (void)p; (void)t; (void)x; (void)y; (void)w; (void)h; return -1;
}
int64_t hpui_ctrl_set_text(int64_t h, const char *t) { (void)h; (void)t; return -1; }
int64_t hpui_ctrl_text(int64_t h, hstr **o) { (void)h; (void)o; return -1; }
int64_t hpui_ctrl_show(int64_t h, bool v) { (void)h; (void)v; return -1; }
int64_t hpui_ctrl_enable(int64_t h, bool v) { (void)h; (void)v; return -1; }
int64_t hpui_ctrl_move(int64_t h, int a, int b, int c, int d) {
    (void)h; (void)a; (void)b; (void)c; (void)d; return -1;
}
int64_t hpui_ctrl_rect(int64_t h, int64_t *x, int64_t *y, int64_t *w, int64_t *ht) {
    (void)h; *x = *y = *w = *ht = 0; return -1;
}
int64_t hpui_ctrl_focus(int64_t h) { (void)h; return -1; }
int64_t hpui_list_add(int64_t h, const char *i) { (void)h; (void)i; return -1; }
int64_t hpui_list_clear(int64_t h) { (void)h; return -1; }
int64_t hpui_list_remove(int64_t h, int64_t i) { (void)h; (void)i; return -1; }
int64_t hpui_list_count(int64_t h, int64_t *o) { (void)h; (void)o; return -1; }
int64_t hpui_list_selected(int64_t h, int64_t *o) { (void)h; (void)o; return -1; }
int64_t hpui_list_select(int64_t h, int64_t i) { (void)h; (void)i; return -1; }
int64_t hpui_list_text(int64_t h, int64_t i, hstr **o) { (void)h; (void)i; (void)o; return -1; }
int64_t hpui_check_get(int64_t h, bool *o) { (void)h; (void)o; return -1; }
int64_t hpui_check_set(int64_t h, bool v) { (void)h; (void)v; return -1; }
int64_t hpui_progress_set(int64_t h, int64_t p) { (void)h; (void)p; return -1; }
int64_t hpui_progress_get(int64_t h) { (void)h; return 0; }
int64_t hpui_slider_get(int64_t h, int64_t *o) { (void)h; (void)o; return -1; }
int64_t hpui_slider_set(int64_t h, int64_t p) { (void)h; (void)p; return -1; }
int64_t hpui_range_set(int64_t h, int64_t a, int64_t b) { (void)h; (void)a; (void)b; return -1; }
int64_t hpui_ctrl_enabled(int64_t h) { (void)h; return 0; }
int64_t hpui_ctrl_checked(int64_t h) { (void)h; return 0; }
int64_t hpui_menu_new(int64_t w, const char *l) { (void)w; (void)l; return -1; }
int64_t hpui_menu_item(int64_t m, const char *l, int64_t i) { (void)m; (void)l; (void)i; return -1; }
int64_t hpui_menu_sep(int64_t m) { (void)m; return -1; }
int64_t hpui_ctrl_set_bg(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_set_fg(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_accent(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_radius(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_style(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_font(int64_t h, int64_t s, bool b) { (void)h; (void)s; (void)b; return -1; }
int64_t hpui_picture_load(int64_t h, const char *p) { (void)h; (void)p; return -1; }
int64_t hpui_theme_set(int64_t w, const int64_t *v, int n) { (void)w; (void)v; (void)n; return -1; }
int64_t hpui_repaint(int64_t h) { (void)h; return -1; }
int64_t hpui_on_event(int64_t h, int64_t e, hval c) { (void)h; (void)e; (void)c; return -1; }
int64_t hpui_timer(int64_t ms, hval c) { (void)ms; (void)c; return -1; }
int64_t hpui_msgbox(int64_t w, const char *t, const char *x, int ty) {
    (void)w; (void)t; (void)x; (void)ty; return 0;
}
hval hpui_open_file(int64_t w, const char *f) { (void)w; (void)f; return hp_null; }
hval hpui_save_file(int64_t w, const char *f) { (void)w; (void)f; return hp_null; }
hval hpui_pick_folder(int64_t w) { (void)w; return hp_null; }
hval hpui_color_pick(int64_t w, int64_t r) { (void)w; (void)r; return hp_null; }
int64_t ui_clip_set(const char *t) { (void)t; return -1; }
hval ui_clip_get(void) { return hp_null; }
#endif
