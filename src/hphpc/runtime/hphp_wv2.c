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
    WV2_GUID(0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xeb, 0x56, 0x51);
static const GUID IID_WV2WebMsgH =
    WV2_GUID(0x57213f19, 0x00e6, 0x49fa, 0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2);
static const GUID IID_WV2EnvDoneH =
    WV2_GUID(0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d);
static const GUID IID_WV2CtlDoneH =
    WV2_GUID(0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xb8, 0x68);

/* ---- vtables: only the slots we call are named. The unnamed ones still
 *      have to occupy their exact position or the offsets drift. -------- */
typedef struct CtlVtbl {
    void *get_IsVisible;                          /*  0 */
    HRESULT (WV2_CALL *put_IsVisible)(void *, BOOL); /*  1 */
    void *get_Bounds;                             /*  2 */
    HRESULT (WV2_CALL *put_Bounds)(void *, RECT); /*  3 */
    HRESULT (WV2_CALL *get_ZoomFactor)(void *, double *);  /* 4 */
    HRESULT (WV2_CALL *put_ZoomFactor)(void *, double);   /* 5 */
    void *SetBoundsAndZoomFactor;                 /*  6 */
    void *MoveFocus;                              /*  7 */
    void *get_ParentWindow;                       /*  8 */
    void *put_ParentWindow;                       /*  9 */
    HRESULT (WV2_CALL *NotifyParentWindowPositionChanged)(void *); /* 10 */
    HRESULT (WV2_CALL *Close)(void *);            /* 11 */
    HRESULT (WV2_CALL *get_CoreWebView2)(void *, void **); /* 12 */
} CtlVtbl;

typedef struct EnvVtbl {
    void *get_BrowserVersionString;                            /* 0 */
    HRESULT (WV2_CALL *CreateCoreWebView2Controller)(void *, HWND, void *, void *); /* 1 */
} EnvVtbl;

typedef struct WebVtbl {
    HRESULT (WV2_CALL *get_Settings)(void *, void **);         /*  0 */
    HRESULT (WV2_CALL *get_Source)(void *, LPWSTR *);           /*  1 */
    HRESULT (WV2_CALL *Navigate)(void *, LPCWSTR);              /*  2 */
    HRESULT (WV2_CALL *NavigateToString)(void *, LPCWSTR);      /*  3 */
    void *add_NavigationStarting;                               /*  4 */
    void *m05;                                                  /*  5 */
    void *m06, *m07;                                            /*  6- 7 */
    void *m08, *m09;                                            /*  8- 9 */
    void *m10, *m11;                                            /* 10-11 */
    void *add_NavigationCompleted;                              /* 12 */
    void *m13;                                                  /* 13 */
    void *m14, *m15, *m16, *m17;                                /* 14-17 */
    void *m18, *m19, *m20, *m21;                                /* 18-21 */
    void *m22, *m23;                                            /* 22-23 */
    HRESULT (WV2_CALL *AddScriptToExecuteOnDocumentCreated)(void *, LPCWSTR, void *); /* 24 */
    void *m25;                                                  /* 25 */
    void *m26, *m27;                                            /* 26-27 */
    HRESULT (WV2_CALL *Reload)(void *);                         /* 28 */
    void *m29, *m30;                                            /* 29-30 */
    void *add_WebMessageReceived;                               /* 31 */
    void *m32, *m33;                                            /* 32-33 */
    void *m34;                                                  /* 34 BrowserProcessId */
    HRESULT (WV2_CALL *get_CanGoBack)(void *, BOOL *);          /* 35 */
    HRESULT (WV2_CALL *get_CanGoForward)(void *, BOOL *);       /* 36 */
    HRESULT (WV2_CALL *GoBack)(void *);                         /* 37 */
    HRESULT (WV2_CALL *GoForward)(void *);                      /* 38 */
    void *m39;                                                  /* 39 */
    HRESULT (WV2_CALL *Stop)(void *);                           /* 40 */
    void *add_NewWindowRequested;                               /* 41 */
    void *m42, *m43, *m44;                                      /* 42-44 */
    HRESULT (WV2_CALL *get_DocumentTitle)(void *, LPWSTR *);    /* 45 */
    void *m46, *m47;                                            /* 46-47 */
    HRESULT (WV2_CALL *OpenDevToolsWindow)(void *);             /* 48 */
    void *m49, *m50, *m51, *m52, *m53, *m54, *m55, *m56, *m57;  /* 49-57 */
} WebVtbl;

typedef struct SetVtbl {
    HRESULT (WV2_CALL *put_IsScriptEnabled)(void *, BOOL);        /*  0 */
    HRESULT (WV2_CALL *put_IsWebMessageEnabled)(void *, BOOL);    /*  1 */
    void *get_IsStatusBarEnabled;                                 /*  2 */
    HRESULT (WV2_CALL *put_IsStatusBarEnabled)(void *, BOOL);     /*  3 */
    void *get_AreDevToolsEnabled;                                 /*  4 */
    HRESULT (WV2_CALL *put_AreDevToolsEnabled)(void *, BOOL);     /*  5 */
    void *m06, *m07, *m08, *m09;
    void *get_IsZoomControlEnabled;                               /* 10 */
    HRESULT (WV2_CALL *put_IsZoomControlEnabled)(void *, BOOL);   /* 11 */
    void *m12, *m13;
} SetVtbl;

typedef struct NavStartArgsVtbl {
    HRESULT (WV2_CALL *get_Uri)(void *, LPWSTR *);              /* 0 */
    void *m01, *m02, *m03;
    HRESULT (WV2_CALL *put_Cancel)(void *, BOOL);               /* 5 */
    void *m06;
} NavStartArgsVtbl;

typedef struct NewWinArgsVtbl {
    HRESULT (WV2_CALL *get_Uri)(void *, LPWSTR *);              /* 0 */
    void *m01, *m02;
    HRESULT (WV2_CALL *put_Handled)(void *, BOOL);              /* 3 */
    void *m04, *m05, *m06, *m07;
} NewWinArgsVtbl;

typedef struct WebMsgArgsVtbl {
    void *m00;
    HRESULT (WV2_CALL *get_WebMessageAsJson)(void *, LPWSTR *); /* 1 */
    void *m02;
} WebMsgArgsVtbl;

typedef struct NavDoneArgsVtbl {
    HRESULT (WV2_CALL *get_IsSuccess)(void *, BOOL *);          /* 0 */
    void *m01;
} NavDoneArgsVtbl;

/* add_* event methods all share one shape */
typedef HRESULT (WV2_CALL *fn_add_ev)(void *, void *, long *);

/* every COM interface starts with a pointer to its vtable */
typedef struct WObj { const void *vt; } WObj;

static const CtlVtbl         *ctl_v(void *o) { return (const CtlVtbl *)((const WObj *)o)->vt; }
static const EnvVtbl         *env_v(void *o) { return (const EnvVtbl *)((const WObj *)o)->vt; }
static const WebVtbl         *web_v(void *o) { return (const WebVtbl *)((const WObj *)o)->vt; }
static const SetVtbl         *set_v(void *o) { return (const SetVtbl *)((const WObj *)o)->vt; }
static const NavStartArgsVtbl *nav_v(void *o) { return (const NavStartArgsVtbl *)((const WObj *)o)->vt; }
static const NewWinArgsVtbl *neww_v(void *o) { return (const NewWinArgsVtbl *)((const WObj *)o)->vt; }
static const WebMsgArgsVtbl *msg_v(void *o) { return (const WebMsgArgsVtbl *)((const WObj *)o)->vt; }
static const NavDoneArgsVtbl *don_v(void *o) { return (const NavDoneArgsVtbl *)((const WObj *)o)->vt; }

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
        if (FAILED(nav_v(args)->get_Uri(args, &wuri))) return S_OK;
        char *uri = utf8_of(wuri);
        if (wuri) CoTaskMemFree(wuri);
        if (!uri) return S_OK;
        /* holybrowser:// never reaches Chromium: cancel it and hand the URL
         * back to the script, which decides what to show instead. */
        if (starts_with_ci(uri, "holybrowser://")) nav_v(args)->put_Cancel(args, TRUE);
        fire(e->cb_nav, uri);
        free(uri);
        return S_OK;
    }
    if (h->kind == HND_NAVDONE) {
        BOOL ok = FALSE;
        don_v(args)->get_IsSuccess(args, &ok);
        fire(e->cb_load, ok ? "1" : "0");
        return S_OK;
    }
    if (h->kind == HND_NEWWIN) {
        neww_v(args)->put_Handled(args, TRUE);   /* tabs are the script's job */
        LPWSTR wuri = NULL;
        if (SUCCEEDED(neww_v(args)->get_Uri(args, &wuri)) && wuri) {
            char *uri = utf8_of(wuri);
            if (uri) { fire(e->cb_new, uri); free(uri); }
        }
        if (wuri) CoTaskMemFree(wuri);
        return S_OK;
    }
    if (h->kind == HND_WEBMSG) {
        LPWSTR wj = NULL;
        if (FAILED(msg_v(args)->get_WebMessageAsJson(args, &wj)) || !wj) return S_OK;
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
        if (FAILED(env_v(result)->CreateCoreWebView2Controller(result, e->parent,
                                                                NULL, g_ctl_done))) {
            g_stage = -1;
            return S_OK;
        }
        g_stage = 1;
        return S_OK;
    }
    e->ctl = result;
    e->ready = true;
    g_stage = 2;
    return S_OK;
}

/* ================= the public API ================= */
typedef HRESULT (WV2_CALL *fn_create_env)(LPCWSTR browserFolder, LPCWSTR userDataFolder,
                                          LPCWSTR args, void *options, void *handler);

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

    HRESULT hr = ((fn_create_env)g_factory)(NULL, wdir,
                                           L"--autoplay-policy=no-user-gesture-required",
                                           NULL, &env_done);
    if (wdir) free(wdir);
    if (FAILED(hr)) return -1;

    /* Bounded, and deliberately short: this runs before the app has an
     * event loop, so a runtime that never answers would otherwise look like
     * a hang. Failing fast lets the caller show a message and carry on. */
    pump_until(12000);
    g_ctl_done = NULL;
    if (!e->ready || !e->ctl) { hpwv2_destroy(e->id); return -1; }

    void *web = NULL;
    if (FAILED(ctl_v(e->ctl)->get_CoreWebView2(e->ctl, &web)) || !web) {
        hpwv2_destroy(e->id);
        return -1;
    }
    e->web = web;

    void *st = NULL;
    if (SUCCEEDED(web_v(web)->get_Settings(web, &st)) && st) {
        const SetVtbl *sv = set_v(st);
        sv->put_IsScriptEnabled(st, TRUE);
        sv->put_IsWebMessageEnabled(st, TRUE);
        sv->put_AreDevToolsEnabled(st, TRUE);
        sv->put_IsStatusBarEnabled(st, FALSE);
        sv->put_IsZoomControlEnabled(st, FALSE);
    }
    web_v(web)->AddScriptToExecuteOnDocumentCreated(web, HB_BRIDGE_JS, NULL);

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
    ((fn_add_ev)web_v(web)->add_NavigationStarting)(web, &e->h[0], &t1);
    ((fn_add_ev)web_v(web)->add_NavigationCompleted)(web, &e->h[1], &t2);
    ((fn_add_ev)web_v(web)->add_NewWindowRequested)(web, &e->h[2], &t3);
    ((fn_add_ev)web_v(web)->add_WebMessageReceived)(web, &e->h[3], &t4);

    return e->id;
}

int64_t hpwv2_destroy(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e) return -1;
    if (e->ctl) { ctl_v(e->ctl)->Close(e->ctl); e->ctl = NULL; }
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
    /* the widget window appears a moment after the controller does */
    if (!e->wvwnd) subclass_widget(e, find_widget(e->parent));
    RECT rc;
    GetClientRect(e->parent, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    int top = MulDiv(top_logical, e->dpi, 96);
    if (top < 0) top = 0;
    if (top > ch) top = ch;

    BOOL vis = (e->shown && !IsIconic(e->parent)) ? TRUE : FALSE;
    ctl_v(e->ctl)->put_IsVisible(e->ctl, vis);
    if (!vis) return 0;
    if (cw == e->last_cw && ch == e->last_ch) return 0;
    e->last_cw = cw;
    e->last_ch = ch;
    POINT pt;
    pt.x = 0;
    pt.y = top;
    ClientToScreen(e->parent, &pt);
    RECT b;
    b.left = pt.x;
    b.top = pt.y;
    b.right = pt.x + cw;
    b.bottom = pt.y + (ch - top);
    ctl_v(e->ctl)->put_Bounds(e->ctl, b);
    ctl_v(e->ctl)->NotifyParentWindowPositionChanged(e->ctl);
    return 0;
}

int64_t hpwv2_navigate(int64_t id, const char *url) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web || !url || !url[0]) return -1;
    wchar_t *w = wide_of(url);
    if (!w) return -1;
    HRESULT hr = web_v(e->web)->Navigate(e->web, w);
    free(w);
    return SUCCEEDED(hr) ? 0 : -1;
}

int64_t hpwv2_html(int64_t id, const char *html) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web || !html) return -1;
    wchar_t *w = wide_of(html);
    if (!w) return -1;
    HRESULT hr = web_v(e->web)->NavigateToString(e->web, w);
    free(w);
    return SUCCEEDED(hr) ? 0 : -1;
}

int64_t hpwv2_back(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    BOOL can = FALSE;
    if (FAILED(web_v(e->web)->get_CanGoBack(e->web, &can)) || !can) return -1;
    return SUCCEEDED(web_v(e->web)->GoBack(e->web)) ? 0 : -1;
}

int64_t hpwv2_forward(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    BOOL can = FALSE;
    if (FAILED(web_v(e->web)->get_CanGoForward(e->web, &can)) || !can) return -1;
    return SUCCEEDED(web_v(e->web)->GoForward(e->web)) ? 0 : -1;
}

int64_t hpwv2_reload(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_v(e->web)->Reload(e->web)) ? 0 : -1;
}

int64_t hpwv2_stop(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_v(e->web)->Stop(e->web)) ? 0 : -1;
}

int64_t hpwv2_can_back(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return 0;
    BOOL can = FALSE;
    web_v(e->web)->get_CanGoBack(e->web, &can);
    return can ? 1 : 0;
}

int64_t hpwv2_can_forward(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return 0;
    BOOL can = FALSE;
    web_v(e->web)->get_CanGoForward(e->web, &can);
    return can ? 1 : 0;
}

int64_t hpwv2_url(int64_t id, char **out) {
    Wv2 *e = wv2_find(id);
    if (out) *out = NULL;
    if (!e || !e->web) return -1;
    LPWSTR w = NULL;
    if (FAILED(web_v(e->web)->get_Source(e->web, &w)) || !w) return -1;
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
    if (FAILED(web_v(e->web)->get_DocumentTitle(e->web, &w)) || !w) return -1;
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
    return SUCCEEDED(ctl_v(e->ctl)->put_ZoomFactor(e->ctl, factor)) ? 0 : -1;
}

int64_t hpwv2_zoom_get(int64_t id, double *out) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    double z = 1.0;
    if (FAILED(ctl_v(e->ctl)->get_ZoomFactor(e->ctl, &z))) return -1;
    if (out) *out = z;
    return 0;
}

int64_t hpwv2_devtools(int64_t id) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->web) return -1;
    return SUCCEEDED(web_v(e->web)->OpenDevToolsWindow(e->web)) ? 0 : -1;
}

/* Show or hide this view. Several controllers share one window, one per
 * tab; only the foreground tab is rendered and takes input. */
int64_t hpwv2_visible(int64_t id, bool on) {
    Wv2 *e = wv2_find(id);
    if (!e || !e->ctl) return -1;
    e->shown = on;
    ctl_v(e->ctl)->put_IsVisible(e->ctl, on ? TRUE : FALSE);
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