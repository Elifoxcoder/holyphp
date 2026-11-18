/* cefshim.cpp - a plain-C ABI over the Chromium Embedded Framework.
 *
 * HolyPHP apps are compiled by gcc into one self-contained executable, so
 * they cannot talk to CEF's C++ API directly. This DLL is the shim: the
 * runtime (hphp_wv2.c) loads it by name, exactly the way it used to load
 * WebView2Loader.dll, and calls the flat functions below.
 *
 * Threading: the caller owns a window, and a child window can only be created
 * by the thread that owns its parent - so CEF runs on that thread
 * (multi_threaded_message_loop=0, external_message_pump=1) and the VM drives
 * it with cefsh_pump() from its own message loop. Callbacks never run script
 * code directly - they are queued and drained by the VM thread, so the hval
 * machinery stays single threaded.
 *
 * Event ids are CEF browser identifiers, which are the same number in the
 * browser and render processes; hpcef_ident maps one back to the id the
 * runtime handed out. Events (see hpwv2_drain_events in hphp_wv2.c):
 *   1 navigation started (text = url)
 *   2 page finished loading (text = "1")
 *   3 page posted through window.chrome.webview.postMessage (text = JSON)
 *   4 window.open(url)
 *   5 key pressed (text = "mod,vk"; mod 1=Ctrl 2=Shift 4=Alt)
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WRAPPING_CEF_SHARED
/* URLDownloadToFileW and friends are gated on the target version; without this
 * they are not even declared. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_client.h"
#include "include/cef_frame.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_v8.h"
#include "include/internal/cef_types_wrappers.h"

#include <windows.h>
#include <wininet.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/* Supplied by the executable (hphp_wv2.c) so that script callbacks only ever
 * run on the VM thread. It is looked up at runtime rather than imported: a
 * static import from an exe resolves against a MODULE of the import-library's
 * name, and the executable is called holybrowser.exe. */
/* HOLYCEF_DEBUG=1 turns on the tracing that answers "what is CEF actually
 * doing": which window a browser became, what it loaded, what it is called.
 * A data: URL is a whole page of base64, so every string that can be one is
 * cut down to its first 96 characters before it reaches the log. */
static int cef_debug_level(void) {
    const char *v = getenv("HOLYCEF_DEBUG");
    if (!v || !v[0] || v[0] == '0') return 0;
    return atoi(v);
}

static bool cef_debug_on(void) { return cef_debug_level() > 0; }

static std::string cef_short(const std::string &s) {
    return s.size() <= 96 ? s : s.substr(0, 93) + "...";
}

/* Tracing has to survive the process boundary. The render process is a
 * separate executable launched by CEF with its own, unrelated stderr, so
 * "no bridge line in run.log" used to prove nothing at all - the line was
 * written, just to a console nobody reads. Everything therefore also goes to
 * one shared file, opened with sharing so the two processes can both hold it.
 * The pid is in every line, so it is obvious which side said what. */
static void cef_debugf(const char *fmt, ...) {
    if (!cef_debug_on()) return;
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 32, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    /* The pid goes FIRST. The callers' format strings already end in \n, and
     * appending after it put every pid on a line of its own. */
    char tail[sizeof(line)];
    snprintf(tail, sizeof(tail), "%s", line);
    int t = (int)strlen(tail);
    while (t > 0 && (tail[t - 1] == '\n' || tail[t - 1] == '\r')) tail[--t] = 0;
    snprintf(line, sizeof(line), "[pid %lu] %s",
             (unsigned long)GetCurrentProcessId(), tail);
    line[sizeof(line) - 1] = 0;
    fputs(line, stderr);
    fputc('\n', stderr);
    fflush(stderr);

    wchar_t tmp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    std::wstring path = std::wstring(tmp) + L"hphp_cef_trace.log";
    /* Win32 rather than the CRT: _wsopen_s takes an extra errno argument under
     * MinGW and not under MSVC, and this file is built with either. */
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string out(line);
    out += '\n';
    DWORD wrote = 0;
    WriteFile(h, out.data(), (DWORD)out.size(), &wrote, NULL);
    CloseHandle(h);
}

typedef long long (*fn_dispatch)(int ev, long long id, const char *text);

/* Defined with the slot bookkeeping below, called once per message-loop turn
 * from cefsh_pump(): finishes closing tabs Chromium leaves half-closed. */
static void closing_poll(void);
static fn_dispatch g_dispatch = NULL;

static void resolve_dispatch(void) {
    if (g_dispatch) return;
    HMODULE self = GetModuleHandleW(NULL);
    if (!self) return;
    g_dispatch = (fn_dispatch)(void *)GetProcAddress(self, "hpcef_dispatch");
}

#define CEFSH_MAX_SLOTS 32

static bool g_inited = false;   /* CEF is up on the VM thread */

/* CEF 154 turned CefBaseRefCounted's four accessors into pure virtuals, so
 * every class we hand to CEF has to answer them. CefRefPtr here is
 * scoped_refptr, which never calls AddRef/Release itself - CEF only holds raw
 * pointers to our handlers - so a counter that no one drives is correct. */
class HpRefCounted : public virtual CefBaseRefCounted {
  public:
    void AddRef() const override { InterlockedIncrement(&ref_); }
    bool Release() const override { return InterlockedDecrement(&ref_) == 0; }
    bool HasOneRef() const override { return ref_ == 1; }
    bool HasAtLeastOneRef() const override { return ref_ >= 1; }

  private:
    mutable volatile LONG ref_ = 1;
};

/* Queued work for the CEF UI thread. */
class CloseBrowserTask : public CefTask, public virtual HpRefCounted {
  public:
    explicit CloseBrowserTask(CefRefPtr<CefBrowser> b) : browser_(b) {}
    void Execute() override {
        if (browser_ && browser_->GetHost()) browser_->GetHost()->CloseBrowser(true);
    }

  private:
    CefRefPtr<CefBrowser> browser_;
};

/* ================= events queued for the VM thread ================= */

struct Ev {
    int ev;
    long long id;
    std::string text;
};

static CRITICAL_SECTION g_ev_lock;
static LONG g_ev_lock_ready = 0;
static std::deque<Ev> g_events;
static DWORD g_main_thread = 0;

static void ev_lock_init(void) {
    if (InterlockedCompareExchange(&g_ev_lock_ready, 1, 0) == 0)
        InitializeCriticalSection(&g_ev_lock);
}

static void push_ev(int ev, long long id, const std::string &text) {
    ev_lock_init();
    EnterCriticalSection(&g_ev_lock);
    g_events.push_back(Ev{ev, id, text});
    LeaveCriticalSection(&g_ev_lock);
    /* Wake the VM's own message loop. The id deliberately differs from
     * HPUI_WM_WAKE: a thread message with no window dispatches to nothing,
     * which is exactly what is wanted here. */
    if (g_main_thread) PostThreadMessageW(g_main_thread, WM_APP + 0x51CE, 0, 0);
}

extern "C" __declspec(dllexport) int cefsh_pump(void) {
    /* CEF runs on the VM thread with external_message_pump, so its work
     * happens here rather than in a CefRunMessageLoop() of our own.
     *
     * The guard matters: an event handler runs script, script can touch the
     * window, the window pumps messages, and that lands back here. Calling
     * CefDoMessageLoopWork() re-entrantly is not allowed. */
    static volatile LONG pumping = 0;
    if (g_inited && InterlockedCompareExchange(&pumping, 1, 0) == 0) {
        CefDoMessageLoopWork();
        InterlockedExchange(&pumping, 0);
    }
    closing_poll();
    ev_lock_init();
    std::deque<Ev> taken;
    EnterCriticalSection(&g_ev_lock);
    taken.swap(g_events);
    LeaveCriticalSection(&g_ev_lock);
    for (size_t i = 0; i < taken.size(); i++) {
        resolve_dispatch();
        if (g_dispatch)
            g_dispatch(taken[i].ev, taken[i].id, taken[i].text.c_str());
    }
    return (int)taken.size();
}

/* ================= JSON, built by hand ================= */

/* CEF 154 dropped CefV8Value::FunctionCall, so JSON.stringify cannot be
 * called from C++. The payload shapes are small; walking the value is enough. */

static void json_str(const std::string &in, std::string &out) {
    out += '"';
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof(b), "\\u%04x", c);
                out += b;
            } else {
                out += (char)c;
            }
        }
    }
    out += '"';
}

static void json_of(CefRefPtr<CefV8Value> v, std::string &out, int depth) {
    if (!v || depth > 8) { out += "null"; return; }
    if (v->IsNull() || v->IsUndefined()) { out += "null"; return; }
    if (v->IsBool()) { out += v->GetBoolValue() ? "true" : "false"; return; }
    if (v->IsInt()) {
        char b[32];
        snprintf(b, sizeof(b), "%d", v->GetIntValue());
        out += b;
        return;
    }
    if (v->IsDouble()) {
        char b[40];
        snprintf(b, sizeof(b), "%.17g", v->GetDoubleValue());
        out += b;
        return;
    }
    if (v->IsString()) { json_str(v->GetStringValue().ToString(), out); return; }
    if (v->IsArray()) {
        int n = v->GetArrayLength();
        out += '[';
        for (int i = 0; i < n; i++) {
            if (i) out += ',';
            json_of(v->GetValue(i), out, depth + 1);
        }
        out += ']';
        return;
    }
    if (v->IsObject()) {
        std::vector<CefString> keys;
        if (!v->GetKeys(keys)) { out += "null"; return; }
        out += '{';
        for (size_t i = 0; i < keys.size(); i++) {
            if (i) out += ',';
            json_str(keys[i].ToString(), out);
            out += ':';
            json_of(v->GetValue(keys[i]), out, depth + 1);
        }
        out += '}';
        return;
    }
    out += "null";
}

/* ================= the JavaScript side of the bridge ================= */

/* The parts of the bridge that can be written in JS. Injected when the page
 * has finished loading, which is early enough: the settings page registers
 * its listener during parsing, and nothing calls back before then. */
static const char *DELIVER_JS =
    "(function(){if(window.__hpDeliver)return;"
    "window.__hpDeliver=function(d){"
    "var wv=window.chrome&&window.chrome.webview;var a=(wv&&wv._l)||[];"
    "for(var i=0;i<a.length;i++){try{a[i]({data:d});}catch(e){}}};})();";

/* holybrowser: links have to be routed back to the script; Chromium has never
 * heard of the scheme. */
static const char *LINK_JS =
    "(function(){if(document.__hpLink)return;document.__hpLink=1;"
    "document.addEventListener('click',function(ev){"
    "var a=ev.target&&ev.target.closest?ev.target.closest('a[href]'):null;"
    "if(!a)return;var h=a.getAttribute('href')||'';"
    "if(h.indexOf('holybrowser:')===0){ev.preventDefault();"
    "if(window.chrome&&window.chrome.webview)"
    "window.chrome.webview.postMessage({hb:'nav',url:h});}},true);})();";

/* ================= instance table (CEF UI thread only) ================= */

class HpClient;

struct Slot {
    long long id;
    int cef_id;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<HpClient> client;
    HWND devtools;                           /* docked, when the user asked */
    std::string favicon;                     /* first icon URL, may be data: */
    std::string title;
};

static Slot g_slots[CEFSH_MAX_SLOTS];
static long long g_next_id = 1;
static int g_live = 0;

static Slot *slot_of(long long id) {
    if (!id) return NULL;
    for (int i = 0; i < CEFSH_MAX_SLOTS; i++)
        if (g_slots[i].id == id && g_slots[i].browser) return &g_slots[i];
    return NULL;
}

static Slot *slot_by_cef_id(int cef_id) {
    if (!cef_id) return NULL;
    for (int i = 0; i < CEFSH_MAX_SLOTS; i++)
        if (g_slots[i].cef_id == cef_id && g_slots[i].browser) return &g_slots[i];
    return NULL;
}

static Slot *slot_new(void) {
    for (int i = 0; i < CEFSH_MAX_SLOTS; i++) {
        if (g_slots[i].browser) continue;
        g_slots[i].id = g_next_id++;
        g_slots[i].cef_id = 0;
        g_slots[i].title.clear();
        return &g_slots[i];
    }
    return NULL;
}

/* Defined with the favicon block further down; a slot that goes away must not
 * leave its icon behind for whatever reuses the index. */
static void fav_forget(int index);

static void slot_release(Slot *s) {
    if (!s) return;
    fav_forget((int)(s - g_slots));
    s->browser = CefRefPtr<CefBrowser>();
    s->client = CefRefPtr<HpClient>();
    s->id = 0;
    s->cef_id = 0;
    s->title.clear();
    /* Not just tidiness: a reused slot that kept the old favicon would show the
       previous page's icon on the new tab until the new page reported one. */
    s->favicon.clear();
}

/* ================= closing a tab =================
 *
 * A browser hosted as a child of a window the runtime owns is not torn down
 * by CefBrowserHost::CloseBrowser(): Chromium posts WM_CLOSE to the OWNING
 * window instead, and the browser stays alive. cefsh_destroy() therefore
 * parks the browser here, and closing_poll() - called from cefsh_pump(), i.e.
 * once per message-loop turn - decides when the window really has to go.
 *
 *   window gone            CEF finished on its own; OnBeforeClose freed the
 *                          slot and there is nothing left to do
 *   still there, in time   CEF is still working; look again next turn
 *   still there, past due  destroy the window ourselves, which is what the
 *                          WM_CLOSE was asking the owner to do; CEF then runs
 *                          OnBeforeClose and frees the slot as usual
 *   forced, still nothing  CEF is not going to answer at all, so free the
 *                          slot here rather than leak one of the 32
 */
struct PendingClose {
    CefRefPtr<CefBrowser> browser;
    int cef_id;
    HWND hwnd;
    ULONGLONG deadline;
    bool forced;
};

static std::vector<PendingClose> g_closing;

static const ULONGLONG CLOSE_GRACE_MS = 400;   /* let CEF close it itself */
static const ULONGLONG CLOSE_FORCE_MS = 2000;  /* then insist, then let go */

static void closing_poll(void) {
    if (g_closing.empty()) return;
    ULONGLONG now = GetTickCount64();
    for (size_t i = g_closing.size(); i-- > 0;) {
        PendingClose &p = g_closing[i];
        if (p.hwnd && !IsWindow(p.hwnd)) {
            cef_debugf("[cef] close: cef_id=%d window gone, CEF tidied up\n", p.cef_id);
            g_closing.erase(g_closing.begin() + i);
            continue;
        }
        if (now < p.deadline) continue;
        if (!p.forced) {
            cef_debugf("[cef] close: cef_id=%d window still there after %llums, "
                       "destroying it\n", p.cef_id, (unsigned long long)CLOSE_GRACE_MS);
            p.forced = true;
            p.deadline = now + CLOSE_FORCE_MS;
            if (p.hwnd) DestroyWindow(p.hwnd);
            continue;
        }
        cef_debugf("[cef] close: cef_id=%d CEF never released it, freeing the "
                   "slot anyway\n", p.cef_id);
        Slot *s = slot_by_cef_id(p.cef_id);
        if (s) slot_release(s);
        g_closing.erase(g_closing.begin() + i);
    }
}

/* ================= handlers ================= */

class HpLifeSpan : public CefLifeSpanHandler, public virtual HpRefCounted {
  public:
    explicit HpLifeSpan(int index) : index_(index) {}
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        Slot *s = &g_slots[index_];
        s->browser = browser;
        s->cef_id = browser->GetIdentifier();
        g_live++;
        cef_debugf("[cef] after-created: cef_id=%d slot=%d live=%d\n",
                   s->cef_id, index_, g_live);
    }
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        cef_debugf("[cef] before-close: cef_id=%d slot=%d live=%d\n",
                   browser->GetIdentifier(), index_, g_live - 1);
        g_live--;
        slot_release(&g_slots[index_]);
    }
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int popup_id,
                       const CefString &target_url,
                       const CefString &target_frame_name,
                       WindowOpenDisposition target_disposition,
                       bool user_gesture,
                       const CefPopupFeatures &popupFeatures,
                       CefWindowInfo &windowInfo,
                       CefRefPtr<CefClient> &client,
                       CefBrowserSettings &settings,
                       CefRefPtr<CefDictionaryValue> &extra_info,
                       bool *no_javascript_access) override {
        /* Tabs are the script's business: report the URL and take over. */
        Slot *s = &g_slots[index_];
        if (s && s->cef_id)
            push_ev(4, s->cef_id,
                    target_url.length() ? target_url.ToString() : std::string());
        return false;
    }

  private:
    int index_;
};

class HpRequest : public CefRequestHandler, public virtual HpRefCounted {
  public:
    explicit HpRequest(int index) : index_(index) {}
    /* Pure in this CEF build. */
    void Select(CefRefPtr<CefX509Certificate> cert) {}
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool user_gesture,
                        bool is_redirect) override {
        Slot *s = &g_slots[index_];
        if (s && s->cef_id && request)
            push_ev(1, s->cef_id, request->GetURL().ToString());
        return false;
    }

  private:
    int index_;
};

class HpLoad : public CefLoadHandler, public virtual HpRefCounted {
  public:
    explicit HpLoad(int index) : index_(index) {}
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                              bool isLoading,
                              bool canGoBack,
                              bool canGoForward) override {
        if (isLoading) return;
        Slot *s = &g_slots[index_];
        if (s && s->cef_id) push_ev(2, s->cef_id, "1");
    }
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,                         int httpStatusCode) override {
        if (!frame || !frame->IsMain()) return;
        std::string url = frame->GetURL().ToString();
        cef_debugf("[cef] load: id=%d url=%s title=%s\n", (int)browser->GetIdentifier(),
                   cef_short(url).c_str(), cef_short(g_slots[index_].title).c_str());
        frame->ExecuteJavaScript(CefString(DELIVER_JS), CefString(url), 0);
        frame->ExecuteJavaScript(CefString(LINK_JS), CefString(url), 0);
    }

  private:
    int index_;
};

/* ================= favicons =================
 * Chromium reports a tab's icon as a URL, but the chrome is drawn by HolyPHP's
 * own toolkit, which can only draw a bitmap it has decoded from a file. So
 * the icon has to be fetched here and handed over as a path.
 *
 * The fetch happens on a worker thread: it is a network round trip and the
 * thread that owns the window is also the thread that draws it. Results land
 * in a locked map rather than in the Slot, because Slot is documented as
 * CEF-UI-thread-only and a worker must never touch it. */

static std::mutex g_fav_lock;
static std::map<int, std::string> g_fav_file;   /* slot index -> local path */
static std::map<std::string, bool> g_fav_inflight;

static std::string fav_dir() {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (!n || n >= MAX_PATH) return std::string();
    std::wstring dir = std::wstring(tmp) + L"hphp_favicons";
    CreateDirectoryW(dir.c_str(), NULL);   /* one level; ignored if it exists */
    /* to utf-8 */
    int m = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, NULL, 0, NULL, NULL);
    if (m <= 1) return std::string();
    std::string out((size_t)m, 0);
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, &out[0], m, NULL, NULL);
    out.resize((size_t)m - 1);
    return out;
}

static bool fav_write(const std::string &dest, const std::string &data);

/* WinINet rather than URLDownloadToFile: the shell helper is IE-era and
 * quietly fails on a redirect to https, which is most of the icons there are.
 * Icons are a few kilobytes, so the whole body is pulled into memory and then
 * written by the caller, which also lets the bytes be sniffed before the file
 * is given a name. */
static long long fav_size_of(const std::string &path) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER li;
    long long n = 0;
    if (GetFileSizeEx(h, &li)) n = (long long)li.QuadPart;
    CloseHandle(h);
    return n;
}

static bool fav_download(const std::string &url, const std::string &dest) {
    HINTERNET net = InternetOpenA("HolyBrowser/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                  NULL, NULL, 0);
    if (!net) {
        cef_debugf("[cef] favicon: InternetOpen failed (%lu)\n", GetLastError());
        return false;
    }
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE;
    HINTERNET u = InternetOpenUrlA(net, url.c_str(), NULL, 0, flags, 0);
    if (!u) {
        cef_debugf("[cef] favicon: open failed %lu for %.80s\n", GetLastError(),
                   url.c_str());
        InternetCloseHandle(net);
        return false;
    }
    char buf[16384];
    DWORD got = 0;
    std::string body;
    while (InternetReadFile(u, buf, sizeof buf, &got) && got > 0) {
        body.append(buf, got);
        if (body.size() > (size_t)512 * 1024) break;   /* an icon, not a page */
    }
    /* Judged by the bytes, not by a status code: a failed icon fetch usually
       comes back as a 200 with an HTML error page in it, and a magic-number
       check catches that where a status check does not. */
    InternetCloseHandle(u);
    InternetCloseHandle(net);
    if (body.empty() || body[0] == '<' || body[0] == '{' || body[0] == '\n') {
        cef_debugf("[cef] favicon: not an image (%d bytes) for %.80s\n",
                   (int)body.size(), url.c_str());
        return false;
    }
    if (!fav_write(dest, body)) return false;
    cef_debugf("[cef] favicon: fetched %d bytes for %.80s\n", (int)body.size(),
               url.c_str());
    return true;
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* data: URLs are the common case - Chromium inlines most icons - and decoding
 * one is a few lines, where a network fetch is not. */
static bool fav_write(const std::string &dest, const std::string &data) {
    HANDLE f = CreateFileA(dest.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, NULL);
    CloseHandle(f);
    return ok && wrote == data.size();
}

/* GDI+ chooses its decoder from the file NAME, not from the bytes: an .ico
 * saved as "icon.png" is not a picture any more, it is a file GDI+ refuses.
 * So every icon is written once under a neutral name, sniffed, and only then
 * given the extension that matches what it actually is. */
static const char *fav_ext_for(const char *path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return ".ico";
    unsigned char head[8];
    memset(head, 0, sizeof head);
    DWORD got = 0;
    ReadFile(f, head, sizeof head, &got, NULL);
    CloseHandle(f);
    if (got >= 4 && head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G')
        return ".png";
    if (got >= 4 && head[0] == 'G' && head[1] == 'I' && head[2] == 'F') return ".gif";
    if (got >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF) return ".jpg";
    if (got >= 4 && head[0] == 0x00 && head[1] == 0x00 && head[2] == 0x01 && head[3] == 0x00)
        return ".ico";
    if (got >= 2 && head[0] == 'B' && head[1] == 'M') return ".bmp";
    return ".ico";
}

static std::string fav_leaf(const std::string &url) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < url.size(); i++) {
        h ^= (unsigned char)url[i];
        h *= 1099511628211ULL;
    }
    char leaf[32];
    snprintf(leaf, sizeof leaf, "%016llx", h);
    return std::string(leaf);
}

/* Move dir\<base>.part to dir\<base><real extension>. Returns the final path. */
static std::string fav_publish(const std::string &dir, const std::string &base) {
    std::string tmp = dir + "\\" + base + ".part";
    const char *ext = fav_ext_for(tmp.c_str());
    std::string dest = dir + "\\" + base + ext;
    /* MOVEFILE_REPLACE_EXISTING, not MoveFile: plain MoveFile FAILS when the
       destination is already there, so the second visit to a site left a .part
       file behind and the icon never arrived again. */
    if (!MoveFileExA(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
        return std::string();
    return dest;
}

static void fav_done(int index, const std::string &url,
                     const std::string &dest, int bytes) {
    if (dest.empty()) return;
    std::lock_guard<std::mutex> lk(g_fav_lock);
    g_fav_inflight.erase(url);
    g_fav_file[index] = dest;
    cef_debugf("[cef] favicon file: slot=%d path=%s (%d bytes)\n", index,
               dest.c_str(), bytes);
}

static void fav_fetch_data(int index, const std::string &url) {
    size_t comma = url.find(',');
    if (comma == std::string::npos) return;
    std::string body = url.substr(comma + 1);
    bool is64 = body.find(";base64") != std::string::npos;
    std::string bytes;
    if (is64) {
        size_t start = comma + 1;
        for (size_t i = 0; i < url.size(); i++)
            if (url[i] == ',' && i > comma) { start = i + 1; break; }
        bytes.reserve((url.size() - start) * 3 / 4 + 4);
        int acc = 0, bits = 0;
        for (size_t i = start; i < url.size(); i++) {
            char c = url[i];
            if (c == '=') break;
            int v = b64val(c);
            if (v < 0) continue;
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                bytes.push_back((char)((acc >> bits) & 0xFF));
            }
        }
    } else {
        size_t start = comma + 1;
        bytes = url.substr(start);
    }
    if (bytes.empty()) return;
    std::string dir = fav_dir();
    if (dir.empty()) return;
    std::string base = fav_leaf(url);
    if (!fav_write(dir + "\\" + base + ".part", bytes)) return;
    fav_done(index, url, fav_publish(dir, base), (int)bytes.size());
}

/* Queue the fetch, at most once per URL. */
static void fav_request(int index, const std::string &url) {
    if (url.empty()) return;
    bool data = url.compare(0, 5, "data:") == 0;
    bool http = url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
    if (!data && !http) return;
    {
        std::lock_guard<std::mutex> lk(g_fav_lock);
        if (g_fav_inflight.count(url)) return;
        g_fav_inflight[url] = true;
    }
    try {
        if (data) {
            std::thread(fav_fetch_data, index, url).detach();
            return;
        }
        std::thread([index, url] {
            std::string dir = fav_dir();
            bool ok = false;
            if (!dir.empty()) {
                std::string part = dir + "\\" + fav_leaf(url) + ".part";
                /* Straight to the network. Writing the URL itself into the
                   file "works" in the sense that the write succeeds, and then
                   GDI+ is handed a text file with a picture's name. */
                ok = fav_download(url, part);
                if (ok) {
                    /* Report the real size: "0 bytes" for an icon that is
                     * demonstrably there is worse than no number at all. */
                    std::string final_path = fav_publish(dir, fav_leaf(url));
                    fav_done(index, url, final_path,
                             (int)fav_size_of(final_path));
                    return;
                }
            }
            std::lock_guard<std::mutex> lk(g_fav_lock);
            g_fav_inflight.erase(url);
        }).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lk(g_fav_lock);
        g_fav_inflight.erase(url);
    }
}

static void fav_forget(int index) {
    std::lock_guard<std::mutex> lk(g_fav_lock);
    g_fav_file.erase(index);
}

class HpDisplay : public CefDisplayHandler, public virtual HpRefCounted {
  public:
    explicit HpDisplay(int index) : index_(index) {}
    /* Chromium hands over a list; the first usable entry is the tab icon. */
    void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                            const std::vector<CefString> &icon_urls) override {
        Slot *sl = &g_slots[index_];
        std::string url;
        for (size_t i = 0; i < icon_urls.size(); i++) {
            CefString u = icon_urls[i];
            if (u.length() && u.ToString() != "about:blank") { url = u.ToString(); break; }
        }
        if (sl && url != sl->favicon) {
            sl->favicon = url;
            cef_debugf("[cef] favicon: id=%d url=%s\n", (int)browser->GetIdentifier(),
                       cef_short(url).c_str());
            fav_request(index_, url);
        }
    }
    void OnTitleChange(CefRefPtr<CefBrowser> browser,
                       const CefString &title) override {
        g_slots[index_].title = title.ToString();
        cef_debugf("[cef] title: id=%d title=%s\n", (int)browser->GetIdentifier(),
                   cef_short(title.ToString()).c_str());
    }

  private:
    int index_;
};

class HpKeyboard : public CefKeyboardHandler, public virtual HpRefCounted {
  public:
    explicit HpKeyboard(int index) : index_(index) {}
    bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                       const CefKeyEvent &event,
                       CefEventHandle os_event,
                       bool *is_keyboard_shortcut) override {
        int mod = 0;
        if (event.modifiers & EVENTFLAG_CONTROL_DOWN) mod |= 1;
        if (event.modifiers & EVENTFLAG_SHIFT_DOWN) mod |= 2;
        if (event.modifiers & EVENTFLAG_ALT_DOWN) mod |= 4;
        if (!mod) return false;
        Slot *s = &g_slots[index_];
        if (!s || !s->cef_id) return false;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d,%d", mod, (int)event.windows_key_code);
        cef_debugf("[cef] key: cef_id=%d mod=%d vk=%d", s->cef_id, mod,
                   (int)event.windows_key_code);
        push_ev(5, s->cef_id, buf);
        return true;
    }

  private:
    int index_;
};

/* ---- the JS bridge, bound in the render process ---- */

/* The page's messages are sent with SendProcessMessage, which is CEF's own
 * channel between the render process and the browser process.
 *
 * The obvious alternatives do not work, and both were tried:
 *   - push_ev() from the render process appends to a static deque inside this
 *     DLL, and every process that loads it gets its own copy of those statics,
 *     so the message landed in a queue nothing ever drained;
 *   - CefPostTask(TID_UI, ...) simply never ran, from the render process.
 *
 * The receiving end is HpClient::OnProcessMessageReceived. */
static const char *BRIDGE_MSG = "hphp:page-message";

/* Set once per render process, from OnContextCreated. One browser per render
 * process, so a single global is enough here. */
static CefRefPtr<CefBrowser> g_bridge_browser;

class BridgeFn : public CefV8Handler, public virtual HpRefCounted {
  public:
    enum Kind { kPost, kAddListener, kRemoveListener };
    BridgeFn(Kind k, int cef_id) : kind_(k), cef_id_(cef_id) {}

    bool Execute(const CefString &name,
                 CefRefPtr<CefV8Value> object,
                 const CefV8ValueList &args,
                 CefRefPtr<CefV8Value> &retval,
                 CefString &exception) override {
        CefRefPtr<CefV8Value> target = object;
        if (!target || !target->IsObject()) return false;
        CefRefPtr<CefV8Value> arr = target->GetValue("_l");
        if (!arr || !arr->IsArray()) {
            arr = CefV8Value::CreateArray(0);
            target->SetValue("_l", arr, V8_PROPERTY_ATTRIBUTE_DONTDELETE);
        }
        switch (kind_) {
        case kPost: {
            if (args.empty()) return true;
            std::string json;
            json_of(args[0], json, 0);
            cef_debugf("[cef] postMessage: cef_id=%d json=%s", cef_id_,
                       cef_short(json).c_str());
            /* This code is RUNNING IN THE RENDER PROCESS, so the message has
               to be handed to the browser process over CEF's own IPC.
               SendProcessMessage is a method of CefFrame, not of CefBrowser,
               and Create() takes only the name - the payload goes into the
               message's argument list, which is what the receiver reads. */
            if (!g_bridge_browser) {
                cef_debugf("[cef] postMessage: DROPPED, no browser\n");
                return true;
            }
            CefRefPtr<CefProcessMessage> pm =
                CefProcessMessage::Create(BRIDGE_MSG);
            CefRefPtr<CefListValue> pargs = pm->GetArgumentList();
            if (pargs) pargs->SetString(0, CefString(json));
            g_bridge_browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, pm);
            return true;
        }
        case kAddListener: {
            if (args.size() < 2) return true;
            if (args[0]->GetStringValue().ToString() != "message") return true;
            if (!args[1] || !args[1]->IsFunction()) return true;
            arr->SetValue(arr->GetArrayLength(), args[1]);
            return true;
        }
        case kRemoveListener: {
            if (args.size() < 2) return true;
            if (args[0]->GetStringValue().ToString() != "message") return true;
            int n = arr->GetArrayLength();
            int w = 0;
            for (int i = 0; i < n; i++) {
                CefRefPtr<CefV8Value> f = arr->GetValue(i);
                if (f && f->IsSame(args[1])) continue;
                arr->SetValue(w++, f);
            }
            for (int i = n; i > w; i--) arr->SetValue(i - 1, CefV8Value::CreateNull());
            return true;
        }
        }
        return true;
    }

  private:
    Kind kind_;
    /* Which browser this document belongs to. A single global for this looked
     * equivalent and was not: every tab installs the bridge, so the last tab to
     * create a document owned the global and messages from every other tab were
     * addressed to it - which is why settings saved from one tab were lost. */
    int cef_id_;
};

/* OnContextCreated runs in the render process, before any page script, which
 * is what makes window.chrome.webview available to every document. */
static void install_bridge(CefRefPtr<CefV8Context> context, int cef_id) {
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    cef_debugf("[cef] bridge: context for cef_id=%d\n", cef_id);

    CefRefPtr<CefV8Value> webview =
        CefV8Value::CreateObject(CefRefPtr<CefV8Accessor>(), CefRefPtr<CefV8Interceptor>());
    webview->SetValue("postMessage",
                      CefV8Value::CreateFunction("postMessage",
                                                 new BridgeFn(BridgeFn::kPost, cef_id)),
                      V8_PROPERTY_ATTRIBUTE_DONTENUM);
    webview->SetValue("addEventListener",
                      CefV8Value::CreateFunction("addEventListener",
                                                 new BridgeFn(BridgeFn::kAddListener, cef_id)),
                      V8_PROPERTY_ATTRIBUTE_DONTENUM);
    webview->SetValue("removeEventListener",
                      CefV8Value::CreateFunction("removeEventListener",
                                                 new BridgeFn(BridgeFn::kRemoveListener, cef_id)),
                      V8_PROPERTY_ATTRIBUTE_DONTENUM);

    CefRefPtr<CefV8Value> chrome = global->GetValue("chrome");
    if (!chrome || !chrome->IsObject())
        chrome = CefV8Value::CreateObject(CefRefPtr<CefV8Accessor>(),
                                          CefRefPtr<CefV8Interceptor>());
    chrome->SetValue("webview", webview, V8_PROPERTY_ATTRIBUTE_DONTENUM);
    global->SetValue("chrome", chrome, V8_PROPERTY_ATTRIBUTE_DONTENUM);
    cef_debugf("[cef] bridge: installed (chrome existed=%d)\n",
               chrome->IsObject() ? 1 : 0);
}

class HpRender : public CefRenderProcessHandler, public virtual HpRefCounted {
  public:
    void OnBrowserCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefDictionaryValue> extra_info) override {}
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        if (!browser || !context) return;
        /* Kept so BridgeFn can send a page's message to this browser, which
         * is what carries the message out of the render process. */
        g_bridge_browser = browser;
        install_bridge(context, browser->GetIdentifier());
    }
};

class HpClient : public CefClient, public virtual HpRefCounted {
  public:
    explicit HpClient(int index)
        : index_(index),
          life_span_(new HpLifeSpan(index)),
          request_(new HpRequest(index)),
          load_(new HpLoad(index)),
          display_(new HpDisplay(index)),
          keyboard_(new HpKeyboard(index)) {}

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return life_span_; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return request_; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return load_; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return display_; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return keyboard_; }

    /* The render process's end of the page bridge finally arriving. */
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override {
        if (!message || !message->IsValid()) return false;
        std::string name = message->GetName();
        if (name != BRIDGE_MSG) return false;
        CefRefPtr<CefListValue> args = message->GetArgumentList();
        std::string arg = args ? args->GetString(0).ToString() : std::string();
        cef_debugf("[cef] bridge post: cef_id=%d json=%s",
                   browser ? (int)browser->GetIdentifier() : -1,
                   cef_short(arg).c_str());
        push_ev(3, browser ? browser->GetIdentifier() : 0, arg);
        return true;
    }

  private:
    int index_;
    CefRefPtr<CefLifeSpanHandler> life_span_;
    CefRefPtr<CefRequestHandler> request_;
    CefRefPtr<CefLoadHandler> load_;
    CefRefPtr<CefDisplayHandler> display_;
    CefRefPtr<CefKeyboardHandler> keyboard_;
};

/* ================= CEF thread, init and shutdown ================= */

/* CEF asks when it may stop pumping. With external_message_pump it will not
 * run by itself, so nudge the VM thread's loop, which calls CefDoMessageLoopWork
 * from hpui_dispatch(). */
class HpProc : public CefBrowserProcessHandler, public virtual HpRefCounted {
  public:
    void OnScheduleMessagePumpWork(int64_t delay_ms) override {
        if (delay_ms < 0) {
            if (g_main_thread) PostThreadMessageW(g_main_thread, WM_APP + 0x51CF, 0, 0);
        } else if (g_main_thread) {
            SetTimer(NULL, 1, (UINT)delay_ms, NULL);
        }
    }
};

class HpApp : public CefApp, public virtual HpRefCounted {
  public:
    void OnBeforeCommandLineProcessing(const CefString &process_type,
                                       CefRefPtr<CefCommandLine> command_line) override {
        /* A background tab in HolyBrowser is not occluded, it is simply not
         * selected - Chromium has to keep painting it. */
        command_line->AppendSwitchWithValue("disable-features",
                                           "CalculateNativeWinOcclusion");
        command_line->AppendSwitch("disable-backgrounding-occluded-windows");
        command_line->AppendSwitch("disable-renderer-backgrounding");
    }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return render_; }
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return proc_; }

  private:
    CefRefPtr<CefRenderProcessHandler> render_ = new HpRender();
    CefRefPtr<CefBrowserProcessHandler> proc_ = new HpProc();
};


/* CefExecuteProcess() and CefInitialize() must both run on the main
 * application thread - CEF refuses to start otherwise, with an ICU data
 * complaint rather than anything that names the real cause. With
 * multi_threaded_message_loop set, CEF then runs its own UI thread, so there
 * is nothing left to do here but return; the caller's own message loop stays
 * in charge of the app's windows. */
static int cef_start(void) {
    CefMainArgs args(GetModuleHandle(NULL));
    CefRefPtr<CefApp> app = new HpApp();

    /* In a subprocess this returns the exit code and the contract is that the
     * process ends HERE. Returning instead is what let every Chromium
     * subprocess go on to run the app's main() and build a second HolyBrowser
     * window, a second tab and a second engine start. */
    int rc = CefExecuteProcess(args, app, NULL);
    if (rc >= 0) {
        fflush(stdout);
        fflush(stderr);
        ExitProcess((UINT)rc);
    }

    CefSettings settings;
    settings.no_sandbox = 1;
    /* The browser window is a real child HWND of a window the runtime owns,
     * and a child window can only be created by the thread that owns its
     * parent. That rules out CEF's separate UI thread: CEF has to run on the
     * VM thread, sharing its message loop through external_message_pump. */
    settings.multi_threaded_message_loop = 0;
    settings.windowless_rendering_enabled = 0;
    settings.external_message_pump = 1;
    const char *logenv = getenv("HOLYCEF_LOG");
    if (logenv && logenv[0]) {
        settings.log_severity = LOGSEVERITY_VERBOSE;
        CefString(&settings.log_file) = logenv;
    } else {
        settings.log_severity = LOGSEVERITY_DISABLE;
    }
    settings.background_color = 0xFF000000;

    /* CEF 120+ takes a singleton lock on root_cache_path, so it must be
     * unique per application instance and must exist up front. */
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring root(exe);
    size_t slash = root.find_last_of(L'\\');
    root = (slash == std::wstring::npos) ? L"." : root.substr(0, slash);
    root += L"\\cefdata";
    CreateDirectoryW(root.c_str(), NULL);
    CefString(&settings.root_cache_path) = root;
    CefString(&settings.cache_path) = root;

    if (!CefInitialize(args, settings, app, NULL)) return -1;
    g_inited = true;
    return 0;
}

extern "C" __declspec(dllexport) int cefsh_init(void) {
    if (g_inited) return 0;
    g_main_thread = GetCurrentThreadId();
    ev_lock_init();
    return cef_start();
}

/* CefShutdown() has to run on the thread that called CefInitialize, which is
 * the thread that called cefsh_init() - the VM's main thread. */
extern "C" __declspec(dllexport) void cefsh_shutdown(void) {
    if (!g_inited) return;
    g_inited = false;
    CefShutdown();
}

/* ================= entry points ================= */

/* CreateBrowserSync() is only legal on the CEF UI thread, which is the VM
 * thread here, so this runs inline - no task, no wait. */
extern "C" __declspec(dllexport) long long cefsh_create(long long parent, const char *dir) {
    (void)dir; /* one profile per process, keyed by root_cache_path */
    bool dbg = cef_debug_on();
    if (dbg) {
        fprintf(stderr, "[cef] create: enter (pid %lu parent=%p)\n",
                (unsigned long)GetCurrentProcessId(), (void *)(intptr_t)parent);
        fflush(stderr);
    }
    if (!g_inited) {
        if (dbg) { fprintf(stderr, "[cef] create: REFUSED, g_inited==0 (pid %lu)\n",
                           (unsigned long)GetCurrentProcessId()); fflush(stderr); }
        return -1;
    }
    Slot *s = slot_new();
    if (!s) {
        if (dbg) { fprintf(stderr, "[cef] create: REFUSED, no free slot (pid %lu)\n",
                           (unsigned long)GetCurrentProcessId()); fflush(stderr); }
        return -1;
    }
    int index = (int)(s - g_slots);
    s->client = new HpClient(index);

    HWND hparent = (HWND)(intptr_t)parent;
    CefWindowInfo info;
    CefRect bounds(0, 0, 800, 600);
    info.SetAsChild((CefWindowHandle)hparent, bounds);
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings bs;
    bs.background_color = 0xFF000000;

    SetLastError(0);
    CefRefPtr<CefBrowser> b = CefBrowserHost::CreateBrowserSync(
        info, s->client, CefString("about:blank"), bs,
        CefRefPtr<CefDictionaryValue>(), CefRefPtr<CefRequestContext>());
    DWORD err = GetLastError();

    if (dbg) {
        HWND child = b ? (HWND)b->GetHost()->GetWindowHandle() : NULL;
        fprintf(stderr,
                "[cef] create: parent=%p live=%d thread=%u own=%u -> browser=%p "
                "id=%d win=%p class=%ls err=%lu slots_used=%d\n",
                (void *)hparent, IsWindow(hparent) ? 1 : 0, GetCurrentThreadId(),
                GetWindowThreadProcessId(hparent, NULL), (void *)b.get(),
                b ? b->GetIdentifier() : -1, (void *)child,
                child ? L"?" : L"", (unsigned long)err, g_live + 1);
        fflush(stderr);
    }

    if (!b) {
        slot_release(s);
        return -1;
    }
    s->browser = b;
    s->cef_id = b->GetIdentifier();
    return s->id;
}

extern "C" __declspec(dllexport) long long cefsh_ident(long long id) {
    Slot *s = slot_of(id);
    return s ? (long long)s->cef_id : 0;
}

extern "C" __declspec(dllexport) long long cefsh_destroy(long long id) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    cef_debugf("[cef] destroy: slot=%lld cef_id=%d\n", id, s->cef_id);
    /* CloseBrowser() on a browser hosted as a child window does not destroy
     * that window: Chromium sends WM_CLOSE to the window that OWNS it, which
     * is the runtime's own window, and then stops. Left alone the tab's window
     * would sit there on top of the next tab and the slot would never come
     * back - 32 open/close cycles and HolyBrowser would refuse every new tab.
     * So the window is hidden right away and g_closing remembers the browser
     * until CEF has really let go of it; closing_poll() finishes the job. */
    CefWindowHandle hw = s->browser->GetHost() ? s->browser->GetHost()->GetWindowHandle() : NULL;
    if (hw) ShowWindow((HWND)hw, SW_HIDE);
    PendingClose pc;
    pc.browser = s->browser;
    pc.cef_id = s->cef_id;
    pc.hwnd = (HWND)hw;
    pc.deadline = GetTickCount64() + CLOSE_GRACE_MS;
    pc.forced = false;
    g_closing.push_back(pc);
    CefPostTask(TID_UI, new CloseBrowserTask(s->browser));
    return 0;
}

/* How many browsers are still on their way out. The runtime asks before it
 * lets a WM_CLOSE reach the script: a tab closing sends that message to the
 * parent window, and it must not be mistaken for the user closing the app. */
/* The tab's site icon, as a path to a local file the toolkit can decode. Once
 * the fetch has landed the script gets the file; until then it gets nothing,
 * because a URL is not something an image loader can open. Returns the byte
 * count, or -1 when this tab has no icon yet. */
extern "C" __declspec(dllexport) int cefsh_favicon(long long id, char *buf,
                                                   int cap) {
    Slot *s = slot_of(id);
    if (!s || !buf || cap < 2) return -1;
    int index = (int)(s - g_slots);
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_fav_lock);
        std::map<int, std::string>::const_iterator it = g_fav_file.find(index);
        if (it != g_fav_file.end()) path = it->second;
    }
    if (path.empty()) return -1;
    int n = (int)path.size();
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, path.c_str(), (size_t)n);
    buf[n] = 0;
    return n;
}

extern "C" __declspec(dllexport) int cefsh_closing(void) {
    return (int)g_closing.size();
}

static int g_fits = 0;
extern "C" __declspec(dllexport) long long cefsh_fit(long long id, int x, int y, int w, int h) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    CefWindowHandle hw = s->browser->GetHost()->GetWindowHandle();
    if (!hw) return -1;
    /* Chromium raises its own window whenever the render widget takes focus,
     * which puts a full-page HWND above the toolbar, the tab strip and the
     * omnibox: every click meant for the chrome landed in the page instead.
     * HWND_BOTTOM pins it under the siblings the embedder owns, which only
     * matters where they do not overlap the page anyway. */
    SetWindowPos((HWND)hw, HWND_BOTTOM, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (cef_debug_level() < 2) return 0;
    /* report in the parent's client coordinates, the ones we were given */
    RECT got;
    GetWindowRect((HWND)hw, &got);
    POINT tl;
    tl.x = got.left; tl.y = got.top;
    ScreenToClient(GetParent((HWND)hw), &tl);
    cef_debugf("[cef] fit#%d want(%d,%d %dx%d) got(%d,%d %dx%d) hwnd=%p\n",
               ++g_fits, x, y, w, h, tl.x, tl.y, got.right - got.left,
               got.bottom - got.top, (void *)hw);
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_go(long long id, const char *url) {
    Slot *s = slot_of(id);
    if (!s || !url) return -1;
    s->browser->GetMainFrame()->LoadURL(CefString(url));
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_html(long long id, const char *html) {
    Slot *s = slot_of(id);
    if (!s || !html) return -1;
    /* CEF has no NavigateToString; a base64 data: URL is the equivalent. */
    std::string in(html);
    static const char *b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < in.size()) v |= (unsigned char)in[i + 1] << 8;
        if (i + 2 < in.size()) v |= (unsigned char)in[i + 2];
        out += b64[(v >> 18) & 63];
        out += b64[(v >> 12) & 63];
        out += (i + 1 < in.size()) ? b64[(v >> 6) & 63] : '=';
        out += (i + 2 < in.size()) ? b64[v & 63] : '=';
    }
    s->browser->GetMainFrame()->LoadURL(CefString("data:text/html;base64," + out));
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_back(long long id) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    s->browser->GoBack();
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_forward(long long id) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    s->browser->GoForward();
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_reload(long long id) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    s->browser->Reload();
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_stop(long long id) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    s->browser->StopLoad();
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_can_back(long long id) {
    Slot *s = slot_of(id);
    return (s && s->browser->CanGoBack()) ? 1 : 0;
}

extern "C" __declspec(dllexport) long long cefsh_can_forward(long long id) {
    Slot *s = slot_of(id);
    return (s && s->browser->CanGoForward()) ? 1 : 0;
}

static char *dup_utf8(const std::string &s) {
    char *b = (char *)malloc(s.size() + 1);
    if (b) memcpy(b, s.c_str(), s.size() + 1);
    return b;
}

extern "C" __declspec(dllexport) long long cefsh_url(long long id, char **out) {
    Slot *s = slot_of(id);
    if (!s || !out) return -1;
    *out = NULL;
    CefString u = s->browser->GetMainFrame()->GetURL();
    if (!u.length()) return -1;
    *out = dup_utf8(u.ToString());
    return *out ? 0 : -1;
}

extern "C" __declspec(dllexport) long long cefsh_title(long long id, char **out) {
    Slot *s = slot_of(id);
    if (!s || !out) return -1;
    *out = NULL;
    if (s->title.empty()) return -1;
    *out = dup_utf8(s->title);
    return *out ? 0 : -1;
}

extern "C" __declspec(dllexport) long long cefsh_zoom(long long id, double factor) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    if (factor < 0.25) factor = 0.25;
    if (factor > 3.0) factor = 3.0;
    s->browser->GetHost()->SetZoomLevel(log2(factor));
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_zoom_get(long long id, double *out) {
    Slot *s = slot_of(id);
    if (!s || !out) return -1;
    *out = pow(2.0, s->browser->GetHost()->GetZoomLevel());
    return 0;
}

/* Devtools opens as a CHILD of the app window, not as a separate popup: a
 * popup lands behind the browser and is a second top-level window, which is
 * exactly what "view devtools on the side" is not. The runtime halves the
 * page and calls cefsh_devtools_fit() once CEF has attached the window. */
/* CEF 154 has no OnDevToolsAttached, so the docked panel is found by asking the
 * parent for its browser-window children: the page is the one we already
 * know, anything else CEF put there is the panel. CEF creates it a moment
 * after ShowDevTools returns, so this is re-asked on every layout tick. */
struct DevScan {
    HWND hit;
};

/* Which class is a CEF browser window?
 *
 * Not "CefBrowserWindow" - that name is from older CEF and matches nothing on
 * this build, which is why the devtools panel used to stay stuck at the origin
 * covering the whole window: cefsh_devtools_fit() was being asked to move a
 * window it could never find. The real classes here are
 * Chrome_WidgetWin_1 (the browser, whose HWND GetWindowHandle returns) and
 * Chrome_RenderWidgetHostHWND (the child it draws into).
 *
 * Note what must NOT be matched: this toolkit registers its own controls
 * under single-letter class names ("B" for a button, "E" for an edit, "I" for
 * an image), which look exactly like a shortened CEF class name from a
 * distance. Matching "C" would have found HolyPHP's own controls. */
static bool cef_browser_class(const wchar_t *cls) {
    return wcscmp(cls, L"Chrome_WidgetWin_1") == 0 ||
           wcscmp(cls, L"CefBrowserWindow") == 0;
}

static BOOL CALLBACK dev_scan(HWND child, LPARAM lp) {
    DevScan *c = (DevScan *)lp;
    wchar_t cls[64];
    GetClassNameW(child, cls, 64);
    if (!cef_browser_class(cls)) return TRUE;
    /* Every OTHER tab is a page too, and with two tabs open a naive "any
     * browser window that is not mine" would pick a tab's page and resize
     * it out from under the user. Only a window that belongs to no live slot
     * can be the devtools panel. */
    for (int i = 0; i < CEFSH_MAX_SLOTS; i++) {
        if (!g_slots[i].browser) continue;
        CefWindowHandle hw = g_slots[i].browser->GetHost()->GetWindowHandle();
        if (hw && (HWND)hw == child) return TRUE;
    }
    c->hit = child;
    return FALSE;
}

static HWND devtools_hwnd(Slot *s) {
    if (!s || !s->browser) return NULL;
    HWND page = (HWND)s->browser->GetHost()->GetWindowHandle();
    if (!page) return NULL;
    HWND par = GetParent(page);
    if (!par) return NULL;
    if (s->devtools && !IsWindow(s->devtools)) s->devtools = NULL;
    DevScan sc;
    sc.hit = NULL;
    EnumChildWindows(par, dev_scan, (LPARAM)&sc);
    s->devtools = sc.hit;
    return s->devtools;
}

extern "C" __declspec(dllexport) long long cefsh_devtools(long long id, int on) {
    Slot *s = slot_of(id);
    if (!s || !s->browser) return -1;
    CefRefPtr<CefBrowserHost> host = s->browser->GetHost();
    if (!host) return -1;
    if (!on) {
        s->devtools = NULL;
        host->CloseDevTools();
        return 0;
    }
    CefWindowInfo info;
    HWND page = (HWND)host->GetWindowHandle();
    RECT pr;
    GetClientRect(GetParent(page), &pr);
    /* CEF only honours this rectangle while the panel is being created, and
       three quarters of the window is not a panel, it is the page with a
       footnote. A third, and never less than 280px, is a usable default. */
    int panel = pr.right * 30 / 100;
    if (panel < 280) panel = 280;
    if (panel > pr.right - 240) panel = pr.right - 240;
    if (panel < 160) panel = 160;
    /* CefRect is (x, y, width, height). Passing pr.right as the width asked
       for a panel that started at 1224 and was 1748 wide, so it ran 300-odd
       pixels off the right-hand edge of a 1748px window - which is where the
       "devtools is a full-screen blank panel" behaviour came from. */
    /* CefRect is (x, y, width, height) - passing pr.right as the width asked for
       a panel that started at 1224 and was 1748 wide, running 300-odd pixels
       off the right-hand edge of a 1748px window.

       And the values are LOGICAL pixels, not device ones: this build is
       per-monitor DPI aware, and handing CEF device pixels made it scale a
       524px panel into 786px. The device-pixel fit in cefsh_devtools_fit()
       still runs on every layout tick and is the authority; this is only the
       geometry the panel starts life with. */
    int dpi = 96;
    HDC pdc = GetDC(GetParent(page));
    if (pdc) {
        int lpx = GetDeviceCaps(pdc, LOGPIXELSX);
        if (lpx > 0) dpi = lpx;
        ReleaseDC(GetParent(page), pdc);
    }
    int px = MulDiv(pr.right - panel, 96, dpi);
    int py = MulDiv(0, 96, dpi);
    int pw = MulDiv(panel, 96, dpi);
    int ph = MulDiv(pr.bottom, 96, dpi);
    CefRect r(px, py, pw, ph);
    info.SetAsChild((CefWindowHandle)GetParent(page), r);
    host->ShowDevTools(info, CefRefPtr<CefClient>(), CefBrowserSettings(),
                       CefPoint());
    return 0;
}

/* 1 when devtools is attached and can be positioned. */
extern "C" __declspec(dllexport) int cefsh_devtools_ready(long long id) {
    Slot *s = slot_of(id);
    return devtools_hwnd(s) ? 1 : 0;
}

extern "C" __declspec(dllexport) long long cefsh_devtools_fit(long long id,
                                                             int x, int y,
                                                             int w, int h) {
    Slot *s = slot_of(id);
    HWND hw = devtools_hwnd(s);
    if (!hw) return -1;
    SetWindowPos(hw, HWND_BOTTOM, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (cef_debug_level() > 1) {
        RECT got;
        GetWindowRect(hw, &got);
        POINT tl;
        tl.x = got.left;
        tl.y = got.top;
        ScreenToClient(GetParent(hw), &tl);
        cef_debugf("[cef] devtools fit: want %d,%d %dx%d got %d,%d %dx%d",
                   x, y, w, h, tl.x, tl.y, got.right - got.left,
                   got.bottom - got.top);
    }
    return 0;
}

/* Print through the system print dialog. */
extern "C" __declspec(dllexport) long long cefsh_print(long long id) {
    Slot *s = slot_of(id);
    if (!s || !s->browser) return -1;
    CefRefPtr<CefBrowserHost> host = s->browser->GetHost();
    if (!host) return -1;
    host->Print();
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_post(long long id, const char *json) {
    Slot *s = slot_of(id);
    if (!s || !json) return -1;
    CefRefPtr<CefFrame> mf = s->browser->GetMainFrame();
    if (!mf) return -1;
    std::string code = "if(window.__hpDeliver)window.__hpDeliver(";
    code += json;
    code += ");";
    mf->ExecuteJavaScript(CefString(code), mf->GetURL(), 0);
    return 0;
}

extern "C" __declspec(dllexport) long long cefsh_visible(long long id, int on) {
    Slot *s = slot_of(id);
    if (!s) return -1;
    CefWindowHandle hw = s->browser->GetHost()->GetWindowHandle();
    if (!hw) return -1;
    /* Focus follows the view only when it actually comes back to the front.
     * The layout tick calls this several times a second, and SetFocus(true)
     * every time pulled the caret out of whatever the user was holding: a
     * toolbar button that lost focus between its WM_LBUTTONDOWN and
     * WM_LBUTTONUP never emitted BN_CLICKED, so no click ever completed. */
    BOOL was_hidden = !IsWindowVisible((HWND)hw);
    ShowWindow((HWND)hw, on ? SW_SHOWNA : SW_HIDE);
    if (on && was_hidden) s->browser->GetHost()->SetFocus(true);
    return 0;
}

/* ================= discovery ================= */

/* libcef.dll sits next to cefshim.dll, which sits next to the executable. */
extern "C" __declspec(dllexport) int cefsh_available(char *buf, int cap) {
    HMODULE self_mod = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(const void *)&cefsh_available, &self_mod))
        return 0;
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(self_mod, self, MAX_PATH)) return 0;
    std::wstring dir(self);
    size_t slash = dir.find_last_of(L'\\');
    dir = (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);
    dir += L"\\libcef.dll";
    if (buf && cap > 0) buf[0] = 0;
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return 0;
    if (buf && cap > 0)
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, buf, cap, NULL, NULL);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
