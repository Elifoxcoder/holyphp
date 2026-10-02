/* hphp_wv2.c — a real Chromium view hosted inside a HolyPHP window.
 *
 * The Edge WebView2 runtime is the only way to get genuine Chromium
 * rendering into a program built with this toolchain: CEF needs a C++
 * toolchain and a completely different link model. WebView2 needs nothing at
 * build time -- we LoadLibrary WebView2Loader.dll and GetProcAddress the one
 * factory function out of it, the same way hphp_ui.c picks up uxtheme.dll and
 * dwmapi.dll.
 *
 * There are no WebView2 headers usable from C (the SDK's WebView2.h is C++),
 * so the vtables below are transcribed from WebView2.idl. The GUIDs and the
 * slot ORDER are the ABI: every slot before the ones we actually call has to
 * be declared, even as an unnamed placeholder, or the offsets shift.
 *
 * Everything here runs on the UI thread. Creating the environment and the
 * controller is asynchronous and the completions arrive through the very
 * message loop that is sitting inside GetMessage, so hpwv2_create() pumps
 * messages until the controller lands instead of deadlocking on it.
 *
 * Layout note: lib/ui.hphp hands out LOGICAL pixels (96 DPI) and the control
 * layer scales them again, so hpwv2_fit() takes the top inset in logical
 * pixels and converts exactly once, here, to the physical pixels that
 * ICoreWebView2Controller::Bounds expects.
 */
#include "hphp_rt.h"

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WV2_CALL __stdcall

#define WV2_GUID(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }

/* ---- event-handler interface ids (from WebView2.idl). We never
 *      QueryInterface for the main objects: they arrive as [retval] out
 *      parameters from the getters below, so their iids are not needed. --- */
static const GUID IID_WV2NavStartingH =
    WV2_GUID(0x9adbe429, 0xf36d, 0x432b, 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3);
static const GUID IID_WV2NavDoneH =
    WV2_GUID(0xd33a35bf, 0x1c49, 0x4f98, 0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c);
static const GUID IID_WV2NewWindowH =
    WV2_GUID(0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51);
static const GUID IID_WV2WebMsgH =
    WV2_GUID(0x57213f19, 0x00e6, 0x49fa, 0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2);
static const GUID IID_WV2EnvDoneH =
    WV2_GUID(0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d);
static const GUID IID_WV2CtlDoneH =
    WV2_GUID(0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c);/* ---- vtables -------------------------------------------------------
 * Every WebView2 interface derives from IUnknown, so slots 0, 1 and 2 are
 * QueryInterface/AddRef/Release and the FIRST declared method lands on
 * slot 3. That offset is the single easiest thing to get wrong by hand,
 * so instead of writing out structs with unnamed filler members (where
 * one miscounted `void *` silently shifts every later call) we keep a
 * plain array of slots and address the ones we want by name.
 *
 * The numbers are the slot positions in the SDK's own
 * ICoreWebView2*Vtbl structs -- Microsoft.Web.WebView2 1.0.3124.44 --
 * which is the ABI we are talking to. Do not renumber them by eye.      */
typedef struct { void *s[52]; } WvWeb;   /* ICoreWebView2           */
typedef struct { void *s[26]; } WvCtl;   /* ICoreWebView2Controller */
typedef struct { void *s[8];  } WvEnv;   /* ICoreWebView2Environment*/
typedef struct { void *s[21]; } WvSet;   /* ICoreWebView2Settings   */
typedef struct { void *s[10]; } WvNavA;  /* ...NavigationStartingEventArgs  */
typedef struct { void *s[6];  } WvDoneA; /* ...NavigationCompletedEventArgs */
typedef struct { void *s[11]; } WvNewA;  /* ...NewWindowRequestedEventArgs   */
typedef struct { void *s[6];  } WvMsgA;  /* ...WebMessageReceivedEventArgs   */

enum {                      /* ICoreWebView2 */
    W_SETTINGS  =  3, W_SOURCE    =  4, W_NAVIGATE =  5, W_NAVSTR  =  6,
    W_ADD_NAVSTART = 7, W_ADD_NAVDONE = 15, W_ADDSCRIPT = 27,
    W_RELOAD    = 31, W_ADD_WEBMSG = 34, W_CANBACK   = 38,
    W_CANFWD    = 39, W_GOBACK    = 40, W_GOFWD     = 41,
    W_STOP      = 43, W_ADD_NEWWIN = 44, W_DOCTITLE  = 48,
    W_DEVTOOLS  = 51
};
enum {                      /* ICoreWebView2Controller */
    C_ISVISIBLE =  4, C_BOUNDS   =  6, C_ZOOMGET  =  7,
    C_ZOOMPUT   =  8, C_NOTIFY   = 23, C_CLOSE    = 24,
    C_GETWEB    = 25
};
enum { E_CREATECTL = 3, E_VERSION = 5 };          /* ICoreWebView2Environment */
enum {                      /* ICoreWebView2Settings */
    S_SCRIPT = 4, S_WEBMSG = 6, S_STATUSBAR = 10,
    S_DEVTOOLS = 12, S_ZOOMCTL = 18
};
enum { A_URI = 3, A_CANCEL = 8 };          /* NavigationStartingEventArgs  */
enum { D_SUCCESS = 3 };                     /* NavigationCompletedEventArgs */
enum { X_URI = 3, X_HANDLED = 6 };          /* NewWindowRequestedEventArgs   */
enum { G_JSON = 4 };                        /* WebMessageReceivedEventArgs   */

/* add_* event methods all share one shape */
typedef HRESULT (WV2_CALL *fn_add_ev)(void *, void *, long *);

/* every COM interface starts with a pointer to its vtable */
typedef struct WObj { const void *vt; } WObj;

static const WvWeb   *web_v (void *o) { return (const WvWeb  *)((const WObj *)o)->vt; }
static const WvCtl   *ctl_v (void *o) { return (const WvCtl  *)((const WObj *)o)->vt; }
static const WvEnv   *env_v (void *o) { return (const WvEnv  *)((const WObj *)o)->vt; }
static const WvSet   *set_v (void *o) { return (const WvSet  *)((const WObj *)o)->vt; }
static const WvNavA  *nav_v (void *o) { return (const WvNavA *)((const WObj *)o)->vt; }
static const WvNewA  *neww_v(void *o) { return (const WvNewA *)((const WObj *)o)->vt; }
static const WvMsgA  *msg_v (void *o) { return (const WvMsgA *)((const WObj *)o)->vt; }
static const WvDoneA *don_v (void *o) { return (const WvDoneA*)((const WObj *)o)->vt; }

/* ---- one typed entry point per call, so the casts live in one place --- */
typedef HRESULT (WV2_CALL *fn_out_obj)(void *, void **);
typedef HRESULT (WV2_CALL *fn_out_wstr)(void *, LPWSTR *);
typedef HRESULT (WV2_CALL *fn_in_wstr)(void *, LPCWSTR);
typedef HRESULT (WV2_CALL *fn_void)(void *);
typedef HRESULT (WV2_CALL *fn_bool_out)(void *, BOOL *);
typedef HRESULT (WV2_CALL *fn_bool_in)(void *, BOOL);
typedef HRESULT (WV2_CALL *fn_dbl_out)(void *, double *);
typedef HRESULT (WV2_CALL *fn_dbl_in)(void *, double);
typedef HRESULT (WV2_CALL *fn_rect_in)(void *, RECT);

static HRESULT web_get_settings(void *o, void **out) {
    return ((fn_out_obj)web_v(o)->s[W_SETTINGS])(o, out);
}
static HRESULT web_get_source(void *o, LPWSTR *out) {
    return ((fn_out_wstr)web_v(o)->s[W_SOURCE])(o, out);
}
static HRESULT web_navigate(void *o, LPCWSTR u) {
    return ((fn_in_wstr)web_v(o)->s[W_NAVIGATE])(o, u);
}
static HRESULT web_navstr(void *o, LPCWSTR u) {
    return ((fn_in_wstr)web_v(o)->s[W_NAVSTR])(o, u);
}
static HRESULT web_addscript(void *o, LPCWSTR js) {
    return ((HRESULT (WV2_CALL *)(void *, LPCWSTR, void *))web_v(o)->s[W_ADDSCRIPT])
           (o, js, NULL);
}
static HRESULT web_can_back(void *o, BOOL *b) {
    return ((fn_bool_out)web_v(o)->s[W_CANBACK])(o, b);
}
static HRESULT web_can_fwd(void *o, BOOL *b) {
    return ((fn_bool_out)web_v(o)->s[W_CANFWD])(o, b);
}
static HRESULT web_goback(void *o)  { return ((fn_void)web_v(o)->s[W_GOBACK])(o); }
static HRESULT web_gofwd(void *o)   { return ((fn_void)web_v(o)->s[W_GOFWD])(o); }
static HRESULT web_reload(void *o)  { return ((fn_void)web_v(o)->s[W_RELOAD])(o); }
static HRESULT web_stop(void *o)    { return ((fn_void)web_v(o)->s[W_STOP])(o); }
static HRESULT web_title(void *o, LPWSTR *out) {
    return ((fn_out_wstr)web_v(o)->s[W_DOCTITLE])(o, out);
}
static HRESULT web_devtools(void *o) { return ((fn_void)web_v(o)->s[W_DEVTOOLS])(o); }
static HRESULT web_add_navstart(void *o, void *h, long *t) {
    return ((fn_add_ev)web_v(o)->s[W_ADD_NAVSTART])(o, h, t);
}
static HRESULT web_add_navdone(void *o, void *h, long *t) {
    return ((fn_add_ev)web_v(o)->s[W_ADD_NAVDONE])(o, h, t);
}
static HRESULT web_add_webmsg(void *o, void *h, long *t) {
    return ((fn_add_ev)web_v(o)->s[W_ADD_WEBMSG])(o, h, t);
}
static HRESULT web_add_newwin(void *o, void *h, long *t) {
    return ((fn_add_ev)web_v(o)->s[W_ADD_NEWWIN])(o, h, t);
}

static HRESULT ctl_visible(void *o, BOOL b) {
    return ((fn_bool_in)ctl_v(o)->s[C_ISVISIBLE])(o, b);
}
static HRESULT ctl_bounds(void *o, RECT r) {
    return ((fn_rect_in)ctl_v(o)->s[C_BOUNDS])(o, r);
}
static HRESULT ctl_notify(void *o) { return ((fn_void)ctl_v(o)->s[C_NOTIFY])(o); }
static HRESULT ctl_close(void *o)  { return ((fn_void)ctl_v(o)->s[C_CLOSE])(o); }
static HRESULT ctl_web(void *o, void **out) {
    return ((fn_out_obj)ctl_v(o)->s[C_GETWEB])(o, out);
}
/* vtable slot 1 of every COM interface is AddRef */
static ULONG ctl_ar_addref(void *o) {
    return ((ULONG (WV2_CALL *)(void *))ctl_v(o)->s[1])(o);
}
static HRESULT ctl_zoom_get(void *o, double *z) {
    return ((fn_dbl_out)ctl_v(o)->s[C_ZOOMGET])(o, z);
}
static HRESULT ctl_zoom_put(void *o, double z) {
    return ((fn_dbl_in)ctl_v(o)->s[C_ZOOMPUT])(o, z);
}

/* ICoreWebView2Environment::CreateCoreWebView2Controller takes only the
 * parent window and the handler; the old options argument is gone. */
static HRESULT env_create_ctl(void *o, HWND parent, void *handler) {
    return ((HRESULT (WV2_CALL *)(void *, HWND, void *))env_v(o)->s[E_CREATECTL])
           (o, parent, handler);
}

static void set_flag(void *o, int slot, BOOL b) {
    ((fn_bool_in)set_v(o)->s[slot])(o, b);
}
static HRESULT nav_uri(void *o, LPWSTR *out) {
    return ((fn_out_wstr)nav_v(o)->s[A_URI])(o, out);
}
static void nav_cancel(void *o) {
    ((fn_bool_in)nav_v(o)->s[A_CANCEL])(o, TRUE);
}
static HRESULT neww_uri(void *o, LPWSTR *out) {
    return ((fn_out_wstr)neww_v(o)->s[X_URI])(o, out);
}
static void neww_handled(void *o) {
    ((fn_bool_in)neww_v(o)->s[X_HANDLED])(o, TRUE);
}
static HRESULT msg_json(void *o, LPWSTR *out) {
    return ((fn_out_wstr)msg_v(o)->s[G_JSON])(o, out);
}
static HRESULT done_success(void *o, BOOL *b) {
    return ((fn_bool_out)don_v(o)->s[D_SUCCESS])(o, b);
}

/* ================= instances ================= */
typedef struct Wv2 Wv2;

enum { HND_NAVSTART = 1, HND_NAVDONE, HND_NEWWIN, HND_WEBMSG };

#define WV2_MAX_KEYS 32
typedef struct Wv2Key Wv2Key;   /* the table itself lives with the shortcuts */
struct Wv2Key {
    int  mod, vk, cmd;
    bool used;
    hval cb;
};


typedef struct Hnd {
    const void *vt;
    long  ref;
    int   kind;
    Wv2 *owner;
} Hnd;

typedef struct HndVtbl {
    HRESULT (WV2_CALL *QueryInterface)(void *, const GUID *, void **);
    ULONG   (WV2_CALL *AddRef)(void *);
    ULONG   (WV2_CALL *Release)(void *);
    HRESULT (WV2_CALL *Invoke)(void *, void *, void *);   /* sender, args */
} HndVtbl;

typedef struct DoneVtbl {
    HRESULT (WV2_CALL *QueryInterface)(void *, const GUID *, void **);
    ULONG   (WV2_CALL *AddRef)(void *);
    ULONG   (WV2_CALL *Release)(void *);
    HRESULT (WV2_CALL *Invoke)(void *, HRESULT, void *);  /* errorCode, result */
} DoneVtbl;

struct Wv2 {
    int64_t id;
    HWND    parent;
    void   *ctl;                 /* ICoreWebView2Controller* */
    void   *web;                 /* ICoreWebView2*          */
    hval    cb_nav, cb_load, cb_msg, cb_new;
    Hnd     h[4];                /* navstart / navdone / newwin / webmsg */
    int     top;                 /* top inset, logical px */
    int     dpi;
    int     last_cw, last_ch;
    /* the rect we last handed the controller, and where we want it: they
     * only agree once the fit correction below has converged */
    RECT    ask;
    bool    asked;
    bool    ready;
    bool    com_init;
    /* keyboard shortcuts: the Chromium widget owns the focus once a page is
     * showing, so the parent's WM_KEYDOWN never fires. We subclass the
     * widget instead and claim the accelerator combinations ourselves. */
    HWND    wvwnd;
    WNDPROC old_proc;
    Wv2Key  keys[WV2_MAX_KEYS];
    bool    shown;          /* false while the tab is in the background */
};

static Wv2 *g_tbl = NULL;
static int g_len = 0, g_cap = 0;
static int64_t g_next = 1;

static Wv2 *wv2_find(int64_t id) {
    for (int i = 0; i < g_len; i++) if (g_tbl[i].id == id) return &g_tbl[i];
    return NULL;
}

/* creation is synchronous and single threaded, so one stage variable is enough */
typedef struct Done Done;   /* defined further down, with the completion vtable */
static volatile int g_stage = 0;      /* 0 = starting, 1 = env, 2 = ctl, -1 = failed */
/* the environment and the controller complete through two DIFFERENT handler
 * objects; reusing one would make the second completion look like the first
 * and ask for another controller forever */
static Done *g_ctl_done = NULL;
static void *g_factory = NULL;
static char  g_loader_path[MAX_PATH * 2];
static int   g_have_factory = 0;
static int   g_searched = 0;

/* ================= small helpers ================= */
static char *utf8_of(const wchar_t *w) {
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return NULL;
    char *out = (char *)malloc((size_t)n);
    if (!out) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

static wchar_t *wide_of(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!out) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

static bool starts_with_ci(const char *s, const char *pfx) {
    if (!s) return false;
    for (size_t i = 0; pfx[i]; i++) {
        char a = s[i], b = pfx[i];
        if (a == 0) return false;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* ---- loader discovery ------------------------------------------------ */
static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void loader_try(const char *path) {
    if (g_have_factory || !path || !path[0] || !file_exists(path)) return;
    HMODULE h = LoadLibraryA(path);
    if (!h) return;
    void *fn = (void *)GetProcAddress(h, "CreateCoreWebView2EnvironmentWithOptions");
    if (!fn) { FreeLibrary(h); return; }
    g_factory = fn;
    g_have_factory = 1;
    lstrcpynA(g_loader_path, path, sizeof g_loader_path);
}

/* newest <root>\<version>\WebView2Loader.dll; versions sort lexicographically */
static void loader_scan_version_dir(const char *root) {
    char dir[MAX_PATH * 4];
    snprintf(dir, sizeof dir, "%.500s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(dir, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    char best[MAX_PATH];
    best[0] = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (strcmp(fd.cFileName, best) > 0) lstrcpynA(best, fd.cFileName, MAX_PATH);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!best[0]) return;
    char p[MAX_PATH * 4];
    snprintf(p, sizeof p, "%.400s\\%.100s\\WebView2Loader.dll", root, best);
    loader_try(p);
}

static void exe_dir(char *out, size_t n) {
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    if (!k || k >= n) { out[0] = 0; return; }
    char *slash = strrchr(out, '\\');
    if (slash) *slash = 0;
}

static void loader_search(void) {
    if (g_searched) return;
    g_searched = 1;

    const char *env = getenv("HOLYWEBVIEW2_LOADER");
    if (env) loader_try(env);

    char p[MAX_PATH * 4], d[MAX_PATH * 2];
    exe_dir(d, sizeof d);
    if (d[0]) {
        snprintf(p, sizeof p, "%s\\WebView2Loader.dll", d);
        loader_try(p);
        snprintf(p, sizeof p, "%s\\webview2\\x64\\WebView2Loader.dll", d);
        loader_try(p);
        snprintf(p, sizeof p, "%s\\..\\webview2\\x64\\WebView2Loader.dll", d);
        loader_try(p);
    }
    /* installed fixed-version runtimes, newest first */
    char roots[3][MAX_PATH * 2];
    int  n = 0;
    const char *pf86 = getenv("ProgramFiles(x86)");
    const char *pf64 = getenv("ProgramFiles");
    const char *la   = getenv("LOCALAPPDATA");
    if (pf86) snprintf(roots[n++], MAX_PATH * 2, "%s\\Microsoft\\EdgeWebView\\Application", pf86);
    if (pf64) snprintf(roots[n++], MAX_PATH * 2, "%s\\Microsoft\\EdgeWebView\\Application", pf64);
    if (la)   snprintf(roots[n++], MAX_PATH * 2, "%s\\Microsoft\\EdgeWebView\\Application", la);
    for (int i = 0; i < n; i++) loader_scan_version_dir(roots[i]);
}

/* ================= guarded callbacks into HolyPHP ================= */
typedef struct Wv2Job {
    hval  cb;
    char *arg;
    int64_t num;
} Wv2Job;

static hval wv2_body(void *env, hval exv) {
    (void)exv;
    Wv2Job *j = (Wv2Job *)env;
    hval a[1];
    a[0] = hp_of_str(hp_str_new(j->arg, strlen(j->arg)));
    return hp_call_value(j->cb, 1, a);
}

static void fire(hval cb, const char *s) {
    if (!s || cb.tag != HV_CLO || !cb.u.c) return;
    Wv2Job j;
    j.cb = cb;
    j.arg = (char *)s;
    hval r = hp_try_run(wv2_body, NULL, &j);
    if (r.tag != HV_NULL) {
        hstr *m = (r.tag == HV_STR) ? r.u.s : hp_val_to_str(r);
        hstr *full = hp_str_concat2(hp_str_lit("[holybrowser] uncaught in event handler: "),
                                    m ? m : hp_str_lit(""));
        fprintf(stderr, "%s\n", full ? full->data : "");
        fflush(stderr);
    }
}

static hval wv2_int_body(void *env, hval exv) {
    (void)exv;
    Wv2Job *j = (Wv2Job *)env;
    hval a[1];
    a[0] = hp_of_int(j->num);
    return hp_call_value(j->cb, 1, a);
}

static void fire_int(hval cb, int64_t n) {
    if (cb.tag != HV_CLO || !cb.u.c) return;
    Wv2Job j;
    j.cb = cb;
    j.arg = NULL;
    j.num = (int)n;
    hval r = hp_try_run(wv2_int_body, NULL, &j);
    if (r.tag != HV_NULL) {
        hstr *m = (r.tag == HV_STR) ? r.u.s : hp_val_to_str(r);
        hstr *full = hp_str_concat2(hp_str_lit("[holybrowser] uncaught in shortcut: "),
                                    m ? m : hp_str_lit(""));
        fprintf(stderr, "%s\n", full ? full->data : "");
        fflush(stderr);
    }
}

/* ================= keyboard shortcuts ================= */
#define WV2_MOD_CTRL  1
#define WV2_MOD_SHIFT 2
#define WV2_MOD_ALT   4

/* one subclassed widget per view; HolyBrowser only ever has one */
#define WV2_MAX_VIEWS 8
typedef struct Wv2Sub {
    bool    used;
    HWND    hwnd;
    WNDPROC old;
    Wv2    *owner;
} Wv2Sub;
static Wv2Sub g_subs[WV2_MAX_VIEWS];

static int mods_down(void) {
    int m = 0;
    if (GetKeyState(VK_CONTROL) < 0) m |= WV2_MOD_CTRL;
    if (GetKeyState(VK_SHIFT)   < 0) m |= WV2_MOD_SHIFT;
    if (GetKeyState(VK_MENU)    < 0) m |= WV2_MOD_ALT;
    return m;
}

static LRESULT CALLBACK wv2_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static void subclass_widget(Wv2 *e, HWND w) {
    if (!w || e->wvwnd == w) return;
    for (int i = 0; i < WV2_MAX_VIEWS; i++) {
        if (g_subs[i].used) continue;
        LONG_PTR prev = SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)wv2_wndproc);
        if (!prev) return;
        g_subs[i].used = true;
        g_subs[i].hwnd = w;
        g_subs[i].old = (WNDPROC)prev;
        g_subs[i].owner = e;
        e->wvwnd = w;
        e->old_proc = (WNDPROC)prev;
        return;
    }
}

static LRESULT CALLBACK wv2_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Wv2Sub *s = NULL;
    for (int i = 0; i < WV2_MAX_VIEWS; i++)
        if (g_subs[i].used && g_subs[i].hwnd == h) { s = &g_subs[i]; break; }
    if (!s) return DefWindowProcW(h, msg, wp, lp);

    if (msg == WM_KEYDOWN && s->owner) {
        Wv2Key *ks = s->owner->keys;
        int mod = mods_down();
        int vk = (int)wp;
        for (int i = 0; i < WV2_MAX_KEYS; i++) {
            if (!ks[i].used || ks[i].vk != vk || ks[i].mod != mod) continue;
            fire_int(ks[i].cb, ks[i].cmd);
            return 0;   /* claimed: the page never sees it */
        }
    }
    return CallWindowProcW(s->old, h, msg, wp, lp);
}

/* The WebView2 widget is a real child of our window; find it by class. */
static HWND find_widget(HWND parent) {
    HWND first = NULL;
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[80];
        GetClassNameW(c, cls, (int)(sizeof cls / sizeof cls[0]));
        if (wcsncmp(cls, L"Chrome_WidgetWin", 15) == 0) return c;
        if (!first) first = c;
    }
    return first;
}

/* ================= our COM objects ================= */
static HRESULT WV2_CALL hnd_QI(void *self, const GUID *id, void **out);
static ULONG   WV2_CALL hnd_AddRef(void *self);
static ULONG   WV2_CALL hnd_Release(void *self);
static HRESULT WV2_CALL hnd_Invoke(void *self, void *sender, void *args);

static const HndVtbl g_hnd_vtbl = { hnd_QI, hnd_AddRef, hnd_Release, hnd_Invoke };

static HRESULT WV2_CALL hnd_QI(void *self, const GUID *id, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    Hnd *h = (Hnd *)self;
    const GUID *mine = NULL;
    switch (h->kind) {
    case HND_NAVSTART: mine = &IID_WV2NavStartingH; break;
    case HND_NAVDONE:  mine = &IID_WV2NavDoneH;    break;
    case HND_NEWWIN:   mine = &IID_WV2NewWindowH;   break;
    case HND_WEBMSG:   mine = &IID_WV2WebMsgH;      break;
    default: return E_NOINTERFACE;
    }
    if (IsEqualGUID(id, &IID_IUnknown) || IsEqualGUID(id, mine)) {
        *out = self;
        hnd_AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WV2_CALL hnd_AddRef(void *self) {
    Hnd *h = (Hnd *)self;
    return (ULONG)(h->ref + 1);
}

static ULONG WV2_CALL hnd_Release(void *self) {
    Hnd *h = (Hnd *)self;
    if (h->ref > 0) h->ref--;
    return (ULONG)h->ref;
}

static HRESULT WV2_CALL hnd_Invoke(void *self, void *sender, void *args) {
    (void)sender;
    Hnd *h = (Hnd *)self;
    Wv2 *e = h ? h->owner : NULL;
    if (!e || !args) return S_OK;

    if (h->kind == HND_NAVSTART) {
        LPWSTR wuri = NULL;
        if (FAILED(nav_uri(args, &wuri))) return S_OK;
        char *uri = utf8_of(wuri);
        if (wuri) CoTaskMemFree(wuri);
        if (!uri) return S_OK;
        /* holybrowser:// never reaches Chromium: cancel it and hand the URL
         * back to the script, which decides what to show instead. */
        if (starts_with_ci(uri, "holybrowser://")) nav_cancel(args);
        fire(e->cb_nav, uri);
        free(uri);
        return S_OK;
    }
    if (h->kind == HND_NAVDONE) {
        BOOL ok = FALSE;
        done_success(args, &ok);
        fire(e->cb_load, ok ? "1" : "0");
        return S_OK;
    }
    if (h->kind == HND_NEWWIN) {
        neww_handled(args);   /* tabs are the script's job */
        LPWSTR wuri = NULL;
        if (SUCCEEDED(neww_uri(args, &wuri)) && wuri) {
            char *uri = utf8_of(wuri);
            if (uri) { fire(e->cb_new, uri); free(uri); }
        }
        if (wuri) CoTaskMemFree(wuri);
        return S_OK;
    }
    if (h->kind == HND_WEBMSG) {
        LPWSTR wj = NULL;
        if (FAILED(msg_json(args, &wj)) || !wj) return S_OK;
        char *json = utf8_of(wj);
        CoTaskMemFree(wj);
        if (json) { fire(e->cb_msg, json); free(json); }
        return S_OK;
    }
    return S_OK;
}

/* ---- the two completion handlers ------------------------------------- */
typedef struct Done {
    const void *vt;
    long ref;
    int  is_env;
    Wv2 *owner;
} Done;

static HRESULT WV2_CALL done_QI(void *self, const GUID *id, void **out);
static ULONG   WV2_CALL done_AddRef(void *self);
static ULONG   WV2_CALL done_Release(void *self);
static HRESULT WV2_CALL done_Invoke(void *self, HRESULT hr, void *result);

static const DoneVtbl g_done_vtbl = { done_QI, done_AddRef, done_Release, done_Invoke };

static HRESULT WV2_CALL done_QI(void *self, const GUID *id, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    Done *d = (Done *)self;
    const GUID *mine = d->is_env ? &IID_WV2EnvDoneH : &IID_WV2CtlDoneH;
    if (IsEqualGUID(id, &IID_IUnknown) || IsEqualGUID(id, mine)) {
        *out = self;
        done_AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WV2_CALL done_AddRef(void *self) {
    Done *d = (Done *)self;
    return (ULONG)(d->ref + 1);
}
static ULONG WV2_CALL done_Release(void *self) {
    Done *d = (Done *)self;
    if (d->ref > 0) d->ref--;
    return (ULONG)d->ref;
}

static HRESULT WV2_CALL done_Invoke(void *self, HRESULT hr, void *result) {
    Done *d = (Done *)self;
    Wv2 *e = d ? d->owner : NULL;
    if (!e) return S_OK;
    if (FAILED(hr) || !result) { g_stage = -1; return S_OK; }
    if (d->is_env) {
        if (!g_ctl_done) { g_stage = -1; return S_OK; }
        if (FAILED(env_create_ctl(result, e->parent, g_ctl_done))) {
            g_stage = -1;
            return S_OK;
        }
        g_stage = 1;
        return S_OK;
    }
    /* The runtime owns a reference to the controller for the duration of this
     * callback and drops it as soon as we return. We keep using the pointer
     * long after that (navigation, bounds, events), so take our own
     * reference or it dangles and every later call reads freed memory. */
    ctl_ar_addref(result);
    e->ctl = result;
    e->ready = true;
    g_stage = 2;
    return S_OK;
}

/* ================= the public API ================= */
/* Since WebView2 SDK 1.0.3xxx the factory takes FOUR arguments: the extra
 * `additionalBrowserArguments` string moved into ICoreWebView2EnvironmentOptions.
 * Calling it with the old five argument shape leaves the handler on the stack
 * where the loader never looks, and it answers E_POINTER. */
typedef HRESULT (WV2_CALL *fn_create_env)(LPCWSTR browserFolder, LPCWSTR userDataFolder,
                                          void *options, void *handler);

static void pump_until(int timeout_ms) {
    DWORD t0 = GetTickCount();
    MSG msg;
    while (g_stage < 2 && g_stage != -1) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if ((GetTickCount() - t0) > (DWORD)timeout_ms) break;
        Sleep(8);
    }
}

/* Keeps clicks on holybrowser:// links away from Chromium's external protocol
 * handler, and gives pages a postMessage channel back to the host. */
static const wchar_t *HB_BRIDGE_JS =
    L"(function(){if(window.__hb)return;window.__hb=1;"
    L"document.addEventListener('click',function(ev){"
    L"var a=ev.target&&ev.target.closest?ev.target.closest('a[href]'):null;"
    L"if(!a)return;var h=a.getAttribute('href')||'';"
    L"if(h.indexOf('holybrowser:')===0){ev.preventDefault();"
    L"if(window.chrome&&window.chrome.webview)"
    L"window.chrome.webview.postMessage(JSON.stringify({hb:'nav',url:h}));}"
    L"},true);})();";

int64_t hpwv2_available(void) {
    loader_search();
    return g_have_factory ? 1 : 0;
}

const char *hpwv2_loader_path(void) {
    loader_search();
    return g_loader_path;
}

int64_t hpwv2_create(int64_t parent_hwnd, const char *user_data_dir) {
    loader_search();
    if (!g_have_factory) return -1;
    HWND parent = (HWND)(intptr_t)parent_hwnd;
    if (!parent || !IsWindow(parent)) return -1;

    if (g_len == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 4;
        g_tbl = (Wv2 *)realloc(g_tbl, (size_t)g_cap * sizeof(Wv2));
        if (!g_tbl) return -1;
    }
    Wv2 *e = &g_tbl[g_len++];
    memset(e, 0, sizeof *e);
    e->id = g_next++;
    e->parent = parent;
    e->cb_nav = e->cb_load = e->cb_msg = e->cb_new = hp_null;
    e->dpi = 96;
    e->shown = true;   /* a fresh view is the foreground one */

    HMODULE us = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *fn_dpi)(HWND);
    fn_dpi get_dpi = us ? (fn_dpi)(void *)GetProcAddress(us, "GetDpiForWindow") : NULL;
    if (get_dpi) {
        UINT d = get_dpi(parent);
        if (d >= 72 && d <= 480) e->dpi = (int)d;
    }

    e->com_init = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

    static Done env_done, ctl_done;
    env_done.vt = &g_done_vtbl; env_done.ref = 1; env_done.is_env = 1; env_done.owner = e;
    ctl_done.vt = &g_done_vtbl; ctl_done.ref = 1; ctl_done.is_env = 0; ctl_done.owner = e;
    g_ctl_done = &ctl_done;

    wchar_t *wdir = wide_of(user_data_dir);
    g_stage = 0;

    /* (browser folder, user data folder, environment options, handler) */
    HRESULT hr = ((fn_create_env)g_factory)(NULL, wdir, NULL, &env_done);
    if (wdir) free(wdir);
    if (FAILED(hr)) return -1;

    /* Bounded: this runs before the app has an event loop, so a runtime that
     * never answers would otherwise look like a hang. Failing fast lets the
     * caller show a message and carry on. The first run on a machine also has
     * to create the profile, which is the slow case. */
    pump_until(25000);
    g_ctl_done = NULL;
    if (!e->ready || !e->ctl) { hpwv2_destroy(e->id); return -1; }

    void *web = NULL;
    if (FAILED(ctl_web(e->ctl, &web)) || !web) {
        hpwv2_destroy(e->id);
        return -1;
    }
    e->web = web;

    void *st = NULL;
    if (SUCCEEDED(web_get_settings(web, &st)) && st) {
        set_flag(st, S_SCRIPT,     TRUE);
        set_flag(st, S_WEBMSG,     TRUE);
        set_flag(st, S_DEVTOOLS,   TRUE);
        set_flag(st, S_STATUSBAR,  FALSE);
        set_flag(st, S_ZOOMCTL,    FALSE);
    }
    web_addscript(web, HB_BRIDGE_JS);

    e->h[0].kind = HND_NAVSTART;
    e->h[1].kind = HND_NAVDONE;
    e->h[2].kind = HND_NEWWIN;
    e->h[3].kind = HND_WEBMSG;
    for (int i = 0; i < 4; i++) {
        e->h[i].vt = &g_hnd_vtbl;
        e->h[i].ref = 1;
        e->h[i].owner = e;
    }
    subclass_widget(e, find_widget(e->parent));

    long t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    web_add_navstart(web, &e->h[0], &t1);
    web_add_navdone(web, &e->h[1], &t2);
    web_add_newwin(web, &e->h[2], &t3);
    web_add_webmsg(web, &e->h[3], &t4);

    return e->id;
}

int64_t hpwv2_destroy(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e) return -1;
    if (e->ctl) { ctl_close(e->ctl); e->ctl = NULL; }
    e->web = NULL;
    if (e->com_init) CoUninitialize();
    for (int i = 0; i < g_len; i++) {
        if (&g_tbl[i] == e) {
            g_tbl[i] = g_tbl[g_len - 1];
            g_len--;
            break;
        }
    }
    return 0;
}

/* Re-fits the view to its parent. `top_logical` is the height of the toolbar
 * the script drew above it, in the same logical pixels lib/ui.hphp uses. */
int64_t hpwv2_fit(int64_t id, int top_logical) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    e->top = top_logical;
    /* the widget window appears a moment after the controller does, and
       Chromium replaces it with a fresh one as the view settles */
    if (!e->wvwnd || !IsWindow(e->wvwnd)) {
        e->wvwnd = NULL;
        subclass_widget(e, find_widget(e->parent));
    }
    RECT rc;
    GetClientRect(e->parent, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    int top = MulDiv(top_logical, e->dpi, 96);
    if (top < 0) top = 0;
    if (top > ch) top = ch;

    BOOL vis = (e->shown && !IsIconic(e->parent)) ? TRUE : FALSE;
    ctl_visible(e->ctl, vis);
    if (!vis) return 0;

    POINT pt;
    pt.x = 0;
    pt.y = top;
    ClientToScreen(e->parent, &pt);
    RECT want;
    want.left   = pt.x;
    want.top    = pt.y;
    want.right  = pt.x + cw;
    want.bottom = pt.y + (ch - top);

    int resized = (cw != e->last_cw || ch != e->last_ch);
    e->last_cw = cw;
    e->last_ch = ch;

    /* Chromium maps the controller rect through its own DPI space, which is
     * not always the space GetClientRect reports. On a 150% display we asked
     * for 1748 physical pixels and the widget turned up 1165 wide and well
     * down and to the right of where it belonged, so assuming the two agree
     * puts the page in the wrong corner.
     *
     * Rather than guess the conversion, ask, look at where the widget
     * actually landed, and close the gap: the fixed point of that correction
     * is the rect we wanted. It settles in a few timer ticks and costs one
     * GetWindowRect, so there is no pumping and no re-entrancy. */
    if (!resized && e->asked && e->wvwnd && IsWindow(e->wvwnd)) {
        RECT got;
        GetWindowRect(e->wvwnd, &got);
        int gw = got.right - got.left, gh = got.bottom - got.top;
        if (gw > 0 && gh > 0) {
            RECT next = e->ask;
            next.left   += (want.left   - got.left);
            next.top    += (want.top    - got.top);
            next.right  += (want.right  - got.right);
            next.bottom += (want.bottom - got.bottom);
            if (next.left != e->ask.left || next.top != e->ask.top ||
                next.right != e->ask.right || next.bottom != e->ask.bottom) {
                e->ask = next;
                ctl_bounds(e->ctl, next);
                ctl_notify(e->ctl);
            }
            return 0;
        }
    }

    e->ask = want;
    e->asked = true;
    ctl_bounds(e->ctl, want);
    ctl_notify(e->ctl);
    return 0;
}

int64_t hpwv2_navigate(int64_t id, const char *url) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web || !url || !url[0]) return -1;
    wchar_t *w = wide_of(url);
    if (!w) return -1;
    HRESULT hr = web_navigate(e->web, w);
    free(w);
    return SUCCEEDED(hr) ? 0 : -1;
}

int64_t hpwv2_html(int64_t id, const char *html) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web || !html) return -1;
    wchar_t *w = wide_of(html);
    if (!w) return -1;
    HRESULT hr = web_navstr(e->web, w);
    free(w);
    return SUCCEEDED(hr) ? 0 : -1;
}

int64_t hpwv2_back(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    BOOL can = FALSE;
    if (FAILED(web_can_back(e->web, &can)) || !can) return -1;
    return SUCCEEDED(web_goback(e->web)) ? 0 : -1;
}

int64_t hpwv2_forward(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    BOOL can = FALSE;
    if (FAILED(web_can_fwd(e->web, &can)) || !can) return -1;
    return SUCCEEDED(web_gofwd(e->web)) ? 0 : -1;
}

int64_t hpwv2_reload(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_reload(e->web)) ? 0 : -1;
}

int64_t hpwv2_stop(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_stop(e->web)) ? 0 : -1;
}

int64_t hpwv2_can_back(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return 0;
    BOOL can = FALSE;
    web_can_back(e->web, &can);
    return can ? 1 : 0;
}

int64_t hpwv2_can_forward(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return 0;
    BOOL can = FALSE;
    web_can_fwd(e->web, &can);
    return can ? 1 : 0;
}

int64_t hpwv2_url(int64_t id, char **out) {
    Wv2 *e = wv2_find(id);
    if (out) *out = NULL;
    if (!e || !e->web) return -1;
    LPWSTR w = NULL;
    if (FAILED(web_get_source(e->web, &w)) || !w) return -1;
    char *u = utf8_of(w);
    CoTaskMemFree(w);
    if (!u) return -1;
    if (out) *out = u; else free(u);
    return 0;
}

int64_t hpwv2_title(int64_t id, char **out) {
    Wv2 *e = wv2_find(id);
    if (out) *out = NULL;
    if (!e || !e->web) return -1;
    LPWSTR w = NULL;
    if (FAILED(web_title(e->web, &w)) || !w) return -1;
    char *t = utf8_of(w);
    CoTaskMemFree(w);
    if (!t) return -1;
    if (out) *out = t; else free(t);
    return 0;
}

int64_t hpwv2_zoom(int64_t id, double factor) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    if (factor < 0.25) factor = 0.25;
    if (factor > 3.0) factor = 3.0;
    return SUCCEEDED(ctl_zoom_put(e->ctl, factor)) ? 0 : -1;
}

int64_t hpwv2_zoom_get(int64_t id, double *out) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    double z = 1.0;
    if (FAILED(ctl_zoom_get(e->ctl, &z))) return -1;
    if (out) *out = z;
    return 0;
}

int64_t hpwv2_devtools(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_devtools(e->web)) ? 0 : -1;
}

/* Show or hide this view. Several controllers share one window, one per
 * tab; only the foreground tab is rendered and takes input. */
int64_t hpwv2_visible(int64_t id, bool on) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    e->shown = on;
    ctl_visible(e->ctl, on ? TRUE : FALSE);
    return 0;
}

/* mod: 1 = Ctrl, 2 = Shift, 4 = Alt (exact combination, no extras allowed).
 * The callback receives `cmd`, so one closure can serve many bindings. */
int64_t hpwv2_shortcut(int64_t id, int64_t mod, int64_t vk, int64_t cmd, hval cb) {
    Wv2 *e = wv2_find(id);
    if (!e) return -1;
    for (int i = 0; i < WV2_MAX_KEYS; i++) {
        Wv2Key *k = &e->keys[i];
        if (k->used) continue;
        k->used = true;
        k->mod = (int)mod;
        k->vk = (int)vk;
        k->cmd = (int)cmd;
        k->cb = cb;
        return 0;
    }
    return -1;
}

/* evtype: 1 = navigating to <url>, 2 = page finished ("1"/"0"),
 *          3 = page posted JSON, 4 = window.open(<url>)                */
int64_t hpwv2_on(int64_t id, int64_t evtype, hval cb) {
    Wv2 *e = wv2_find(id);
    if (!e) return -1;
    switch (evtype) {
    case 1: e->cb_nav = cb; break;
    case 2: e->cb_load = cb; break;
    case 3: e->cb_msg = cb; break;
    case 4: e->cb_new = cb; break;
    default: return -1;
    }
    return 0;
}

#else  /* !_WIN32 — inert stubs so the language still checks on POSIX */

int64_t hpwv2_available(void) { return 0; }
const char *hpwv2_loader_path(void) { return ""; }
int64_t hpwv2_create(int64_t p, const char *d) { (void)p; (void)d; return -1; }
int64_t hpwv2_destroy(int64_t i) { (void)i; return -1; }
int64_t hpwv2_fit(int64_t i, int t) { (void)i; (void)t; return -1; }
int64_t hpwv2_navigate(int64_t i, const char *u) { (void)i; (void)u; return -1; }
int64_t hpwv2_html(int64_t i, const char *h) { (void)i; (void)h; return -1; }
int64_t hpwv2_back(int64_t i) { (void)i; return -1; }
int64_t hpwv2_forward(int64_t i) { (void)i; return -1; }
int64_t hpwv2_reload(int64_t i) { (void)i; return -1; }
int64_t hpwv2_stop(int64_t i) { (void)i; return -1; }
int64_t hpwv2_can_back(int64_t i) { (void)i; return 0; }
int64_t hpwv2_can_forward(int64_t i) { (void)i; return 0; }
int64_t hpwv2_url(int64_t i, char **o) { (void)i; if (o) *o = NULL; return -1; }
int64_t hpwv2_title(int64_t i, char **o) { (void)i; if (o) *o = NULL; return -1; }
int64_t hpwv2_zoom(int64_t i, double z) { (void)i; (void)z; return -1; }
int64_t hpwv2_zoom_get(int64_t i, double *o) { (void)i; if (o) *o = 1.0; return -1; }
int64_t hpwv2_devtools(int64_t i) { (void)i; return -1; }
int64_t hpwv2_on(int64_t i, int64_t e, hval c) { (void)i; (void)e; (void)c; return -1; }
int64_t hpwv2_visible(int64_t i, bool on) { (void)i; (void)on; return -1; }
int64_t hpwv2_shortcut(int64_t i, int64_t m, int64_t v, int64_t c, hval cb) {
    (void)i; (void)m; (void)v; (void)c; (void)cb; return -1;
}

#endif /* _WIN32 */