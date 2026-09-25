/* hphp_ui.c — HolyPHP native UI layer (Win32).
 *
 * Pure C, no language concepts: int64 handles in a runtime-side table,
 * user callbacks are hval closures invoked through hp_call_value. Every
 * callback runs under hp_try_run so a HolyPHP exception inside a handler
 * prints and keeps the app alive instead of unwinding through C.
 *
 * Two operating modes:
 *   - ui_dispatch(ms): pump events for up to ms milliseconds (run mode,
 *     keeps console I/O alive, script stays in its own loop)
 *   - ui_run_main(): blocking GetMessage loop until ui_quit() or the last
 *     window closes (build mode: behaves like a normal desktop app)
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

/* ---------------- handle table ---------------- */
#ifdef _WIN32
typedef struct UiEntry {
    int64_t id;
    int kind;
    HWND hwnd;
    HMENU menu_id;          /* menu item command id (menu items only) */
    hval cb_click, cb_change, cb_action, cb_key;
    bool checked_flag;
} UiEntry;

static UiEntry *tbl = NULL;
static size_t tbl_len = 0, tbl_cap = 0;
static int64_t next_id = 1;
static HBRUSH g_bg_brush = NULL;   /* shared background brush (set_bg) */

static UiEntry *tbl_find(int64_t id) {
    for (size_t i = 0; i < tbl_len; i++)
        if (tbl[i].id == id) return &tbl[i];
    return NULL;
}

static void tbl_add(int64_t id, int kind, HWND hwnd) {
    if (tbl_len == tbl_cap) {
        tbl_cap = tbl_cap ? tbl_cap * 2 : 32;
        tbl = (UiEntry *)realloc(tbl, tbl_cap * sizeof(UiEntry));
    }
    tbl[tbl_len].id = id;
    tbl[tbl_len].kind = kind;
    tbl[tbl_len].hwnd = hwnd;
    tbl[tbl_len].menu_id = NULL;
    tbl[tbl_len].cb_click = hp_null;
    tbl[tbl_len].cb_change = hp_null;
    tbl[tbl_len].cb_action = hp_null;
    tbl[tbl_len].cb_key = hp_null;
    tbl[tbl_len].checked_flag = false;
    tbl_len++;
}
#endif /* _WIN32 */

/* ---------------- init / event loop ---------------- */
#ifdef _WIN32
static void ensure_common(void) {
    static bool done = false;
    if (!done) {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof icc;
        icc.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES |
                    ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
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
        /* block until something arrives; wakeups return immediately */
        for (;;) {
            MSG msg;
            if (!GetMessageW(&msg, NULL, 0, 0)) return 0;
            if (msg.message == HPUI_WM_WAKE) return 1;
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    /* timed window: pump what is pending, then wait up to the remainder */
    DWORD start = GetTickCount();
    for (;;) {
        while (pump_one(false)) { }
        DWORD spent = GetTickCount() - start;
        if (spent >= (DWORD)ms) return 1;
        MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(ms - spent), QS_ALLINPUT);
    }
}

void hpui_run_main(void) {
    while (pump_one(true)) { }
}

void hpui_quit(void) { PostQuitMessage(0); }

/* ---------------- window ---------------- */
static LRESULT CALLBACK ui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void register_classes(void) {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = ui_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"HolyPHPWnd";
    RegisterClassW(&wc);
    done = true;
}

int64_t hpui_window_new(const char *title, int x, int y, int w, int h) {
    ensure_common();
    register_classes();
    if (x == -32000) {   /* sentinel: center on screen */
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        if (y < 0) y = 0;
    }
    int64_t id = next_id++;
    tbl_add(id, HPUI_WINDOW, NULL);
    wchar_t wtitle[512];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wtitle, 512);
    HWND hw = CreateWindowExW(0, L"HolyPHPWnd", wtitle,
                              WS_OVERLAPPEDWINDOW,
                              x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    UiEntry *e = tbl_find(id);
    if (e) e->hwnd = hw;
    if (hw) {
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)id);
        ShowWindow(hw, SW_SHOW);
        UpdateWindow(hw);
    }
    return id;
}

/* ---------------- callback invocation ---------------- */
static void ui_call_guarded(hval cb, hval *args, int nargs);

static void call_cb(hval cb, int64_t arg1, int64_t arg2) {
    if (cb.tag != HV_CLO || !cb.u.c) return;
    hval args[2];
    args[0] = hp_of_int(arg1);
    args[1] = hp_of_int(arg2);
    /* exceptions inside a handler must not unwind through C: catch, print,
     * keep running (the app stays alive, like a good GUI should) */
    ui_call_guarded(cb, args, 2);
}

/* trampolines for hp_try_run: run a closure with (id, aux) args */
typedef struct CbJob {
    hval cb;
    hval args[2];
    int nargs;
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
        /* unhandled exception from the handler: report and continue */
        hstr *m = (r.tag == HV_STR) ? r.u.s : hp_val_to_str(r);
        hstr *full = hp_str_concat2(hp_str_lit("[hphp ui] uncaught in event handler: "),
                                    m ? m : hp_str_lit(""));
        fprintf(stderr, "%s\n", full ? full->data : "");
        fflush(stderr);
    }
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
    SetWindowPos(e->hwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
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

/* ---------------- control creation ---------------- */
int64_t hpui_ctrl_new(int64_t kind, int64_t parent, const char *text,
                      int x, int y, int w, int h) {
    UiEntry *p = tbl_find(parent);
    if (!p || !p->hwnd) return -1;
    ensure_common();
    int64_t id = next_id++;
    HWND hw = NULL;
    wchar_t wt[512];
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, wt, 512);
    DWORD wstyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
    switch (kind) {
    case HPUI_BUTTON:
        hw = CreateWindowExW(0, L"BUTTON", wt, wstyle | BS_PUSHBUTTON,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_LABEL:
        hw = CreateWindowExW(0, L"STATIC", wt, wstyle | SS_LEFT,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_INPUT:
        hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wt,
                             wstyle | ES_AUTOHSCROLL | ES_LEFT,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_CHECKBOX:
        hw = CreateWindowExW(0, L"BUTTON", wt, wstyle | BS_AUTOCHECKBOX,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_LIST:
        hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", wt,
                             wstyle | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                             WS_VSCROLL,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_COMBO:
        hw = CreateWindowExW(0, L"COMBOBOX", wt,
                             wstyle | CBS_DROPDOWNLIST | WS_VSCROLL,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_PROGRESS: {
        hw = CreateWindowExW(0, PROGRESS_CLASSW, NULL, wstyle | PBS_SMOOTH,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(hw, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        break;
    }
    case HPUI_SLIDER: {
        hw = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                             wstyle | TBS_HORZ | TBS_AUTOTICKS,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(hw, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 100));
        break;
    }
    case HPUI_GROUP:
        hw = CreateWindowExW(0, L"BUTTON", wt, wstyle | BS_GROUPBOX,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    case HPUI_PANEL:
        hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", NULL, wstyle | SS_BLACKFRAME,
                             x, y, w, h, p->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break;
    default:
        return -1;
    }
    if (!hw) return -1;
    tbl_add(id, (int)kind, hw);
    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)id);
    return id;
}

int64_t hpui_ctrl_set_text(int64_t h, const char *text) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    MultiByteToWideChar(CP_UTF8, 0, text ? text : "", -1, w, 1024);
    SetWindowTextW(e->hwnd, w);
    return 0;
}

int64_t hpui_ctrl_text(int64_t h, hstr **out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    int n = GetWindowTextLengthW(e->hwnd);
    wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (!w) return -1;
    GetWindowTextW(e->hwnd, w, n + 1);
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(need ? need : 1);
    if (u && need) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, need, NULL, NULL);
    free(w);
    if (!u) return -1;
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
    return 0;
}

int64_t hpui_ctrl_move(int64_t h, int x, int y, int w, int hgt) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    MoveWindow(e->hwnd, x, y, w, hgt, TRUE);
    return 0;
}

int64_t hpui_ctrl_focus(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    SetFocus(e->hwnd);
    return 0;
}

/* ---------------- lists / combos ---------------- */
int64_t hpui_list_add(int64_t h, const char *item) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    MultiByteToWideChar(CP_UTF8, 0, item ? item : "", -1, w, 1024);
    if (e->kind == HPUI_COMBO)
        return (int64_t)SendMessageW(e->hwnd, CB_ADDSTRING, 0, (LPARAM)w);
    return (int64_t)SendMessageW(e->hwnd, LB_ADDSTRING, 0, (LPARAM)w);
}

int64_t hpui_list_clear(int64_t h) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    SendMessageW(e->hwnd, e->kind == HPUI_COMBO ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
    return 0;
}

int64_t hpui_list_remove(int64_t h, int64_t index) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (e->kind == HPUI_COMBO)
        return SendMessageW(e->hwnd, CB_DELETESTRING, (WPARAM)index, 0);
    return SendMessageW(e->hwnd, LB_DELETESTRING, (WPARAM)index, 0);
}

int64_t hpui_list_count(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = e->kind == HPUI_COMBO
        ? (int64_t)SendMessageW(e->hwnd, CB_GETCOUNT, 0, 0)
        : (int64_t)SendMessageW(e->hwnd, LB_GETCOUNT, 0, 0);
    return 0;
}

int64_t hpui_list_selected(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    LRESULT r = e->kind == HPUI_COMBO
        ? SendMessageW(e->hwnd, CB_GETCURSEL, 0, 0)
        : SendMessageW(e->hwnd, LB_GETCURSEL, 0, 0);
    *out = r == LB_ERR ? -1 : (int64_t)r;
    return 0;
}

int64_t hpui_list_select(int64_t h, int64_t index) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (e->kind == HPUI_COMBO)
        SendMessageW(e->hwnd, CB_SETCURSEL, (WPARAM)index, 0);
    else
        SendMessageW(e->hwnd, LB_SETCURSEL, (WPARAM)index, 0);
    return 0;
}

int64_t hpui_list_text(int64_t h, int64_t index, hstr **out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    wchar_t w[1024];
    LRESULT n;
    if (e->kind == HPUI_COMBO) {
        n = SendMessageW(e->hwnd, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (n == CB_ERR || n >= 1024) return -1;
        SendMessageW(e->hwnd, CB_GETLBTEXT, (WPARAM)index, (LPARAM)w);
    } else {
        n = SendMessageW(e->hwnd, LB_GETTEXTLEN, (WPARAM)index, 0);
        if (n == LB_ERR || n >= 1024) return -1;
        SendMessageW(e->hwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)w);
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(need ? need : 1);
    if (u && need) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, need, NULL, NULL);
    if (!u) return -1;
    *out = hp_str_new(u, (size_t)(need > 0 ? need - 1 : 0));
    free(u);
    return 0;
}

/* ---------------- checkboxes / progress / slider ---------------- */
int64_t hpui_check_get(int64_t h, bool *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = SendMessageW(e->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return 0;
}

int64_t hpui_check_set(int64_t h, bool on) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    SendMessageW(e->hwnd, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return 0;
}

int64_t hpui_progress_set(int64_t h, int64_t pct) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    SendMessageW(e->hwnd, PBM_SETPOS, (WPARAM)pct, 0);
    return 0;
}

int64_t hpui_slider_get(int64_t h, int64_t *out) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    *out = (int64_t)SendMessageW(e->hwnd, TBM_GETPOS, 0, 0);
    return 0;
}

int64_t hpui_slider_set(int64_t h, int64_t pos) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    SendMessageW(e->hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);
    return 0;
}

/* ---------------- colors / fonts ---------------- */
int64_t hpui_ctrl_set_bg(int64_t h, int64_t rgb) {
    UiEntry *e = tbl_find(h);
    if (!e) return -1;
    if (g_bg_brush) { DeleteObject(g_bg_brush); g_bg_brush = NULL; }
    g_bg_brush = CreateSolidBrush(RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
    return 0;
}

int64_t hpui_ctrl_set_fg(int64_t h, int64_t rgb) {
    (void)h; (void)rgb;
    return 0; /* full per-control colors need owner draw; kept simple */
}

int64_t hpui_ctrl_font(int64_t h, int64_t size, bool bold) {
    UiEntry *e = tbl_find(h);
    if (!e || !e->hwnd) return -1;
    HFONT f = CreateFontW((int)-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                          FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(e->hwnd, WM_SETFONT, (WPARAM)f, MAKELPARAM(TRUE, 0));
    return 0;
}

int64_t hpui_picture_load(int64_t h, const char *path) {
    (void)h; (void)path;
    return -1; /* images via GDI+ come later; keep the API surface now */
}

/* ---------------- menus ---------------- */
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
    e->hwnd = w->hwnd;      /* store owner window for lookup */
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

int64_t hpui_ctrl_checked(int64_t h) {
    UiEntry *e = tbl_find(h);
    return (e && e->checked_flag) ? 1 : 0;
}

/* ---------------- events ---------------- */
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

static void fire(UiEntry *e, hval *slot, int64_t a1, int64_t a2) {
    if (slot && slot->tag == HV_CLO && slot->u.c) call_cb(*slot, a1, a2);
}

/* ---------------- timers ---------------- */
#define HPUI_TIMER_ID_BASE 1000

int64_t hpui_timer(int64_t ms, hval cb) {
    if (ms <= 0) return -1;
    UiEntry *w = NULL;
    /* attach the timer to the first live window */
    for (size_t i = 0; i < tbl_len; i++) {
        if (tbl[i].kind == HPUI_WINDOW && tbl[i].hwnd && IsWindow(tbl[i].hwnd)) {
            w = &tbl[i];
            break;
        }
    }
    if (!w) return -1;
    static int64_t tid = HPUI_TIMER_ID_BASE;
    int64_t id = tid++;
    tbl_add(id, HPUI_WINDOW, NULL);   /* bookkeeping entry for the closure */
    UiEntry *e = tbl_find(id);
    e->cb_click = cb;
    SetTimer(w->hwnd, (UINT_PTR)id, (UINT)ms, NULL);
    return id;
}

/* ---------------- dialogs ---------------- */
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

static wchar_t *pick_path(HWND owner, bool save, const char *filter) {
    wchar_t fname[1024];
    fname[0] = 0;
    wchar_t wfilter[1024];
    /* Windows needs NUL-separated pairs "desc\0*.ext\0\0"; accept "|" syntax */
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
    if (!ok) return NULL;
    int need = WideCharToMultiByte(CP_UTF8, 0, fname, -1, NULL, 0, NULL, NULL);
    wchar_t *out = (wchar_t *)malloc((need + 1) * sizeof(wchar_t));
    if (out) WideCharToMultiByte(CP_UTF8, 0, fname, -1, (char *)out, need, NULL, NULL);
    /* return as UTF-8 in a char buffer; cast back at the wrapper */
    return out;
}

hval hpui_open_file(int64_t win, const char *filter) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    wchar_t *got = pick_path(owner, false, filter);
    if (!got) return hp_null;
    char *u8 = (char *)got;
    hval r = hp_of_str(hp_str_new(u8, strlen(u8)));
    free(got);
    return r;
}

hval hpui_save_file(int64_t win, const char *filter) {
    UiEntry *w = tbl_find(win);
    HWND owner = (w && w->hwnd) ? w->hwnd : NULL;
    wchar_t *got = pick_path(owner, true, filter);
    if (!got) return hp_null;
    char *u8 = (char *)got;
    hval r = hp_of_str(hp_str_new(u8, strlen(u8)));
    free(got);
    return r;
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
    int need = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *u8 = (char *)malloc(need ? need : 1);
    if (u8 && need) WideCharToMultiByte(CP_UTF8, 0, path, -1, u8, need, NULL, NULL);
    hval r = hp_of_str(hp_str_new(u8, strlen(u8)));
    free(u8);
    return r;
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

/* ---------------- clipboard ---------------- */
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

/* did the HSCROLL notification come from the thumb (end of tracking)? */
static bool code_slider_end(WPARAM wp) {
    int code = LOWORD(wp);
    return code == TB_ENDTRACK || code == TB_THUMBPOSITION;
}

/* ---------------- window proc: routes events ---------------- */
static LRESULT CALLBACK ui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int64_t id = (int64_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    UiEntry *e = id ? tbl_find(id) : NULL;

    switch (msg) {
    case HPUI_WM_WAKE:
        return 0;
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
            if (code == BN_CLICKED && se->kind == HPUI_CHECKBOX) {
                bool on = SendMessageW(src, BM_GETCHECK, 0, 0) == BST_CHECKED;
                fire(se, &se->cb_change, sid, on ? 1 : 0);
                return 0;
            }
            if ((code == EN_CHANGE && se->kind == HPUI_INPUT) ||
                (code == CBN_SELCHANGE && se->kind == HPUI_COMBO)) {
                fire(se, &se->cb_change, sid, 0);
                return 0;
            }
            if (code == LBN_SELCHANGE && se->kind == HPUI_LIST) {
                fire(se, &se->cb_change, sid, 0);
                return 0;
            }
        }
        /* menu items arrive with lp == 0 and wp == command id */
        for (size_t i = 0; i < tbl_len; i++) {
            if (tbl[i].kind == HPUI_MENUITEM && tbl[i].menu_id == (HMENU)wp) {
                fire(&tbl[i], &tbl[i].cb_click, tbl[i].id, (int64_t)wp);
                return 0;
            }
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
    case WM_HSCROLL: {
        HWND src = (HWND)lp;
        int64_t sid = src ? (int64_t)GetWindowLongPtrW(src, GWLP_USERDATA) : 0;
        if (!sid) {
            /* keyboard scrolls arrive with lp == 0: use the focused control */
            HWND f = GetFocus();
            if (f) sid = (int64_t)GetWindowLongPtrW(f, GWLP_USERDATA);
        }
        UiEntry *se = sid ? tbl_find(sid) : NULL;
        if (se && se->kind == HPUI_SLIDER) {
            int code = LOWORD(wp);
            bool live = code == TB_THUMBTRACK || code == TB_THUMBPOSITION ||
                        code == TB_ENDTRACK   || code == SB_LINELEFT ||
                        code == SB_LINERIGHT  || code == SB_PAGELEFT ||
                        code == SB_PAGERIGHT  || code == SB_TOP      ||
                        code == SB_BOTTOM;
            if (live) {
                int64_t pos = 0;
                /* SB_THUMBTRACK/POSITION carry the position in the hiword;
                 * query the control for everything else (arrow keys etc.) */
                if (code == TB_THUMBTRACK || code == TB_THUMBPOSITION)
                    pos = HIWORD(wp);
                else
                    hpui_slider_get(sid, &pos);
                fire(se, &se->cb_change, sid, pos);   /* fires continuously while dragging */
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        if (g_bg_brush) {
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)g_bg_brush;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_CLOSE:
        if (e) fire(e, &e->cb_action, id, 0);   /* 0 arg2: allow veto via ? */
        if (e && e->cb_action.tag == HV_CLO) return 0;   /* handler decides */
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        int windows_left = 0;
        for (size_t i = 0; i < tbl_len; i++)
            if (tbl[i].kind == HPUI_WINDOW && tbl[i].hwnd &&
                tbl[i].id != id && IsWindow(tbl[i].hwnd))
                windows_left++;
        if (e) e->hwnd = NULL;
        if (windows_left == 0) PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

#endif /* _WIN32 */

/* ---------------- non-Windows stubs (API-complete, inert) ---------------- */
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
int64_t hpui_slider_get(int64_t h, int64_t *o) { (void)h; (void)o; return -1; }
int64_t hpui_slider_set(int64_t h, int64_t p) { (void)h; (void)p; return -1; }
int64_t hpui_ctrl_checked(int64_t h) { (void)h; return 0; }
int64_t hpui_menu_new(int64_t w, const char *l) { (void)w; (void)l; return -1; }
int64_t hpui_menu_item(int64_t m, const char *l, int64_t i) { (void)m; (void)l; (void)i; return -1; }
int64_t hpui_menu_sep(int64_t m) { (void)m; return -1; }
int64_t hpui_ctrl_set_bg(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_set_fg(int64_t h, int64_t r) { (void)h; (void)r; return -1; }
int64_t hpui_ctrl_font(int64_t h, int64_t s, bool b) { (void)h; (void)s; (void)b; return -1; }
int64_t hpui_picture_load(int64_t h, const char *p) { (void)h; (void)p; return -1; }
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
