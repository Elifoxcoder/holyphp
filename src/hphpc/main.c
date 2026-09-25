/* main.c — the `hphp` driver: the HolyPHP language toolchain.
 *
 * One self-contained binary. The C runtime sources and the hphp_rt.h header
 * are embedded inside hphp.exe at build time and extracted into a private
 * per-user cache directory on demand — an installed hphp.exe needs nothing
 * else on the machine. Users never see intermediate files: .hphp source goes
 * in, a native executable (or its output) comes out.
 *
 *   hphp file.hphp              run a HolyPHP program (default command)
 *   hphp run file.hphp          same, keeps argv pass-through
 *   hphp build file.hphp -o app build a native executable
 *   hphp check file.hphp        type- & borrow-check only
 *   hphp emit file.hphp         inspect the lowered internal representation
 *   hphp version                print version
 *
 * Set HPHP_VERBOSE=1 to see backend commands, HPHP_KEEP_BIN=1 to keep
 * executables produced by `run`.
 */
#include "parser.h"
#include "sema.h"
#include "codegen.h"
#include "builtins.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HPHP_VERSION "0.2.0"

/* Used by install/search/publish when the user has not configured a registry
 * (no registry_url file, no $HPHP_REGISTRY). Hosted alongside the project;
 * see registry/README.md. */
#define PKG_DEFAULT_REGISTRY "http://registry.holyphp.org:8930"

#include "runtime_embed.h" /* the runtime, embedded in this binary */
#include "pkgcore.h"
#include "pkgnet.h"

#if defined(_WIN32)
#include <windows.h>
static void nap_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <unistd.h>
static void nap_ms(int ms) { usleep((useconds_t)ms * 1000); }
#endif

/* package-manager hook: where an `import "name/..."` finds installed packages.
 * Layout in the pkg home: packages/<name>/<entry or name>.hphp. Also accepts
 * plain "name.hphp" when name is an installed package. Returns malloc'd path
 * or NULL. Defined here so it can use read_file-free path logic; the actual
 * file read is done by the caller. */
static char *pkg_import_candidate(const char *spec) {
    /* form: "<pkg>/file.hphp" or "<pkg>.hphp" or "<pkg>" (-> entry/<pkg>.hphp) */
    char *slash = strchr(spec, '/');
    if (slash) {
        char pkg[128];
        size_t pl = (size_t)(slash - spec);
        if (pl == 0 || pl >= sizeof pkg) return NULL;
        memcpy(pkg, spec, pl); pkg[pl] = 0;
        if (!pkg_is_installed(pkg)) return NULL;
        char *dir = pkg_dir_of(pkg);
        char *cand = fmt("%s/%s", dir, slash + 1);
        free(dir);
        return cand;
    }
    const char *stem = spec;
    size_t sl = strlen(spec);
    char stembuf[128];
    if (sl > 5 && !strcmp(spec + sl - 5, ".hphp")) {
        if (sl - 5 >= sizeof stembuf) return NULL;
        memcpy(stembuf, spec, sl - 5); stembuf[sl - 5] = 0;
        stem = stembuf;
    }
    if (!pkg_is_installed(stem)) return NULL;
    /* prefer the manifest entry, then <pkg>.hphp */
    char *dir = pkg_dir_of(stem);
    char *mf = fmt("%s/" PKG_MANIFEST, dir);
    char *cand = NULL;
    FILE *f = fopen(mf, "rb");
    if (f) {
        char js[4096];
        size_t got = fread(js, 1, sizeof js - 1, f);
        js[got] = 0;
        fclose(f);
        /* entry from manifest */
        const char *ek = strstr(js, "\"entry\"");
        if (ek) {
            const char *p = strchr(ek + 7, '"');
            if (p) {
                const char *e = strchr(p + 1, '"');
                if (e && e > p + 1) {
                    char entry[256];
                    size_t el = (size_t)(e - p - 1);
                    if (el < sizeof entry) {
                        memcpy(entry, p + 1, el); entry[el] = 0;
                        cand = fmt("%s/%s", dir, entry);
                    }
                }
            }
        }
    }
    if (!cand) cand = fmt("%s/%s.hphp", dir, stem);
    free(mf); free(dir);
    return cand;
}

/* like read_file_or_die, but a missing file is not an error (import search) */
static char *read_file_soft(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

static char *read_file_or_die(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fatal("cannot open '%s'", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------ */
/* paths                                                               */
/* ------------------------------------------------------------------ */

static const char *path_file_name(const char *p) {
    const char *slash = strrchr(p, '/'), *bslash = strrchr(p, '\\');
    const char *cut = slash > bslash ? slash : bslash;
    return cut ? cut + 1 : p;
}

static char *path_join(const char *a, const char *b) {
    size_t n = strlen(a);
    if (n && (a[n - 1] == '/' || a[n - 1] == '\\')) return fmt("%s%s", a, b);
    return fmt("%s/%s", a, b);
}

/* ------------------------------------------------------------------ */
/* hphp private directory (runtime extraction + build cache)           */
/* ------------------------------------------------------------------ */

static int system_sh(const char *cmd); /* fwd */

static char *hphp_dir(void) {
    static char *dir = NULL;
    if (dir) return dir;
    const char *base = getenv("LOCALAPPDATA");
#ifdef _WIN32
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) base = getenv("TEMP");
#else
    if (!base || !*base) base = getenv("XDG_CACHE_HOME");
    if (!base || !*base) base = getenv("HOME");
#endif
    if (base && *base)
        dir = fmt("%s/.hphp", base);
    else
        dir = xstrdup(".hphp");
#ifdef _WIN32
    system_sh(fmt("if not exist \"%s\" mkdir \"%s\"", dir, dir));
#else
    system_sh(fmt("mkdir -p \"%s\"", dir));
#endif
    return dir;
}

/* Per-invocation scratch directory: fixed short path (no spaces!), reused
 * across runs. Compilation and linking happen from inside it so command
 * lines only ever contain simple relative paths. */
static const char *work_dir(void) {
#ifdef _WIN32
    static const char *wd = "C:/hphpbuild";
    (void)hphp_dir();
    return wd;
#else
    return hphp_dir(); /* POSIX has no space problems */
#endif
}

/* ------------------------------------------------------------------ */
/* command execution                                                   */
/* ------------------------------------------------------------------ */

/* Run cmd quietly; output discarded. */
static int system_sh(const char *cmd) {
    int verbose = getenv("HPHP_VERBOSE") != NULL;
#ifdef _WIN32
    /* Parenthesize so cmd.exe cannot strip the outer quotes of the command
     * word (it mangles quoted paths otherwise). */
    char *q = verbose ? fmt("(%s) 2>&1", cmd) : fmt("(%s) > nul 2>&1", cmd);
#else
    char *q = verbose ? fmt("(%s) 2>&1", cmd) : fmt("(%s) >/dev/null 2>&1", cmd);
#endif
    if (verbose) fprintf(stderr, "[hphp backend] %s\n", cmd);
    int rc = system(q);
    free(q);
    return rc;
}

static int file_nonempty(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz > 0;
}

/* A "successful" link can still silently produce a corrupt/empty exe when a
 * scanner interferes with freshly written files. Detect a real PE image. */
static int file_looks_like_exe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char sig[2] = {0, 0};
    size_t got = fread(sig, 1, 2, f);
    fclose(f);
    if (got != 2 || sig[0] != 0x4D || sig[1] != 0x5A) return 0;
#ifdef _WIN32
    /* On Windows, files opened by a scanner can briefly fail to open again;
     * require the size to be stable across a short interval. */
    fseek(f, 0, SEEK_END);
#endif
    return 1;
}

static void remove_quiet(const char *path) { remove(path); }

/* Move src onto dst with C stdio rename; remove dst first if needed. */
static int move_file(const char *src, const char *dst) {
    remove_quiet(dst);
    if (rename(src, dst) == 0) return 0;
    /* cross-device fallback: copy */
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return -1; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    if (fclose(b) != 0) return -1;
    remove_quiet(src);
    return 0;
}

/* First line of `gcc -print-file-name=...` (or -print-libgcc-file-name etc). */
static char *gcc_query(const char *args) {
#ifdef _WIN32
    char *cmd = fmt("(gcc %s) 2>nul", args);
#else
    char *cmd = fmt("(gcc %s) 2>/dev/null", args);
#endif
    FILE *p = popen(cmd, "r");
    free(cmd);
    if (!p) return xstrdup("");
    char buf[1024] = {0};
    if (!fgets(buf, sizeof buf, p)) buf[0] = 0;
    pclose(p);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return xstrdup(buf);
}

static const char *path_dir_of(const char *p) {
    static char dir[1024];
    const char *slash = strrchr(p, '/'), *bslash = strrchr(p, '\\');
    const char *cut = slash > bslash ? slash : bslash;
    if (!cut) return ".";
    size_t n = (size_t)(cut - p);
    if (n >= sizeof dir) n = sizeof dir - 1;
    memcpy(dir, p, n);
    dir[n] = 0;
    return dir;
}

/* ------------------------------------------------------------------ */
/* embedded runtime: hphp.exe carries its own standard runtime         */
/* ------------------------------------------------------------------ */

/* Locate this very executable (argv[0] can be relative or bare). */
static char *find_compiler(const char *argv0) {
#ifdef _WIN32
    char *buf = xmalloc(4096);
    DWORD n = GetModuleFileNameA(NULL, buf, 4096);
    if (n > 0 && n < 4096) return buf;
    free(buf);
#endif
    if (strchr(argv0, '/') || strchr(argv0, '\\')) return xstrdup(argv0);
    /* bare name: search PATH */
    const char *pathenv = getenv("PATH");
    if (pathenv) {
        char *paths = xstrdup(pathenv);
        for (char *tok = strtok(paths, ";:"); tok; tok = strtok(NULL, ";:")) {
#ifdef _WIN32
            char *cand = fmt("%s/%s.exe", tok, argv0);
#else
            char *cand = fmt("%s/%s", tok, argv0);
#endif
            FILE *f = fopen(cand, "rb");
            if (f) { fclose(f); return cand; }
        }
    }
    return xstrdup(argv0);
}

/* runtime/<file> next to the compiler binary, or the embedded copy */
static char *runtime_path(const char *self, const char *file) {
    char *p = fmt("%s/runtime/%s", path_dir_of(self), file);
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return p; }
    return NULL; /* fall back to the embedded copy */
}

static char *embed_read(const char *self, const char *file) {
    char *p = runtime_path(self, file);
    if (p) return read_file_or_die(p);
    if (strcmp(file, "hphp_rt.c") == 0) return xstrdup(hp_embed_rt_c);
    if (strcmp(file, "hphp_std.c") == 0) return xstrdup(hp_embed_std_c);
    if (strcmp(file, "hphp_ui.c") == 0) return xstrdup(hp_embed_ui_c);
    if (strcmp(file, "hphp_rt.h") == 0) return xstrdup(hp_embed_rt_h);
    fatal("hphp: unknown runtime file '%s'", file);
    return NULL;
}

#ifdef _WIN32
static void mkdir_p(const char *dir) {
    system_sh(fmt("if not exist \"%s\" mkdir \"%s\"", dir, dir));
}
#else
static void mkdir_p(const char *dir) { system_sh(fmt("mkdir -p \"%s\"", dir)); }
#endif

/* Write the three runtime files into <wd>/rt. Overwrite unconditionally so
 * the runtime always matches the compiler version. */
static void extract_runtime(const char *wd, const char *self) {
    static const char *files[] = {"hphp_rt.c", "hphp_std.c", "hphp_ui.c", "hphp_rt.h"};
    mkdir_p(wd);
    char *rtdir = path_join(wd, "rt");
    mkdir_p(rtdir);
#ifdef _WIN32
    system_sh(fmt("if not exist \"%s\" mkdir \"%s\"", rtdir, rtdir));
#else
    system_sh(fmt("mkdir -p \"%s\"", rtdir));
#endif
    for (int i = 0; i < 4; i++) {
        char *data = embed_read(self, files[i]);
        char *dest = fmt("%s/%s", rtdir, files[i]);
        FILE *f = fopen(dest, "wb");
        if (!f) fatal("hphp: cannot write runtime to build dir (%s)", dest);
        fwrite(data, 1, strlen(data), f);
        fclose(f);
        free(dest);
        free(data);
    }
}

/* ------------------------------------------------------------------ */
/* backend: compile + link inside the work dir                         */
/* ------------------------------------------------------------------ */

/* Compile the three C inputs in wd (relative paths only) and link them to
 * <wd>/prog.out, then move the finished image to exepath. Never links
 * directly onto exepath — freshly-written exes are scanner bait; the final
 * rename is atomic-ish and safe. */
static int cc_build_in_dir(const char *wd, const char *rt, const char *cpath,
                           const char *exepath) {
    /* all paths below are relative to wd */
    const char *po = "prog.o", *ro = "hp_rt.o", *so = "hp_std.o", *uo = "hp_ui.o";
    const char *out = "prog.out";
    char *out_abs = path_join(wd, out);
    int verbose = getenv("HPHP_VERBOSE") != NULL;
    (void)rt; (void)cpath;

    /* work from inside wd so command lines stay short and quote-free */
#ifdef _WIN32
    char prev[1024] = {0};
    GetCurrentDirectoryA(sizeof prev, prev);
    if (!SetCurrentDirectoryA(wd)) {
        fprintf(stderr, "hphp: cannot enter build dir %s\n", wd);
        return -1;
    }
#else
    char *prev = getcwd(NULL, 0);
    if (chdir(wd) != 0) {
        fprintf(stderr, "hphp: cannot enter build dir %s\n", wd);
        return -1;
    }
#endif

    for (int attempt = 1; attempt <= 3; attempt++) {
        /* --- compile the three objects (gcc -c is rock solid) --- */
        bool compiled = true;
        const char *ccs[4] = {"prog.c:prog.o", "rt/hphp_rt.c:hp_rt.o",
                              "rt/hphp_std.c:hp_std.o", "rt/hphp_ui.c:hp_ui.o"};
        for (int i = 0; i < 4; i++) {
            char src[128], obj[64];
            const char *colon = strchr(ccs[i], ':');
            snprintf(src, sizeof src, "%.*s", (int)(colon - ccs[i]), ccs[i]);
            snprintf(obj, sizeof obj, "%s", colon + 1);
            char *cmd = fmt("gcc -O2 -std=c11 -I rt -c %s -o %s", src, obj);
            int rc = system_sh(cmd);
            free(cmd);
            char *objp = path_join(wd, obj);
            if (rc != 0 || !file_nonempty(objp)) {
                fprintf(stderr,
                        "hphp: internal error: cannot compile %s "
                        "(set HPHP_VERBOSE=1 for details)\n", src);
                compiled = false;
                break;
            }
        }
        if (!compiled) { nap_ms(300 * attempt); continue; }

        /* --- link: mingw ld directly, plain gcc as fallback --- */
        char *crt2 = gcc_query("-print-file-name=crt2.o");
        bool have_ld = crt2[0] && strchr(crt2, '/') != NULL;
        int rc = -1;
        if (have_ld) {
            char *libgcc = gcc_query("-print-libgcc-file-name");
            char *crtbegin = gcc_query("-print-file-name=crtbegin.o");
            char *crtend = gcc_query("-print-file-name=crtend.o");
            char *manifest = gcc_query("-print-file-name=default-manifest.o");
            char *ld = gcc_query("-print-prog-name=ld");
            const char *gccdir = path_dir_of(libgcc);
            const char *crtdir = path_dir_of(crt2);
            char *link = fmt(
                "\"%s\" -m i386pep -Bdynamic -o %s %s %s "
                "-L\"%s\" -L\"%s\" %s %s %s %s "
                "-lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32 "
                "-lws2_32 -lpthread -ladvapi32 -lshell32 -luser32 "
                "-lgdi32 -lcomctl32 -lcomdlg32 -lole32 \"%s\" \"%s\"",
                ld, out, crt2, crtbegin, gccdir, crtdir,
                po, ro, so, uo, manifest, crtend);
            rc = system_sh(link);
            free(link);
        }
        if (rc != 0 || !file_nonempty(out_abs)) {
            char *link = fmt("gcc -O2 %s %s %s %s -o %s -lm -lws2_32 "
                             "-lgdi32 -lcomctl32 -lcomdlg32 -lole32",
                             po, ro, so, uo, out);
            rc = system_sh(link);
        }
        if (rc == 0 && file_looks_like_exe(out_abs)) {
#ifdef _WIN32
            SetCurrentDirectoryA(prev);
#else
            chdir(prev);
#endif
            /* link finished in wd; place the finished image at exepath */
            if (verbose) fprintf(stderr, "[hphp backend] moving %s -> %s\n", out, exepath);
            remove_quiet(exepath);
            if (move_file(out_abs, exepath) == 0 && file_looks_like_exe(exepath))
                return 0;
            fprintf(stderr, "hphp: cannot place executable at %s\n", exepath);
            return -1;
        }
        fprintf(stderr, "hphp: backend link attempt %d failed (output %s)\n",
                attempt, file_looks_like_exe(out_abs) ? "ok" : "invalid/missing");
        remove_quiet(out_abs);
        nap_ms(500 * attempt);
    }
#ifdef _WIN32
    SetCurrentDirectoryA(prev);
#else
    chdir(prev);
#endif
    return -1;
}

/* Spawn exepath with an inherited console and wait; returns its exit code. */
static int spawn_wait(const char *exepath, PtrVec args) {
#ifdef _WIN32
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof si;
    Buf cmdline;
    buf_init(&cmdline);
    buf_printf(&cmdline, "\"%s\"", exepath);
    for (size_t i = 0; i < args.len; i++)
        buf_printf(&cmdline, " \"%s\"", (const char *)args.items[i]);
    if (!CreateProcessA(NULL, cmdline.data, NULL, NULL, TRUE, 0, NULL, NULL,
                        &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    Buf cmdline;
    buf_init(&cmdline);
    buf_printf(&cmdline, "\"%s\"", exepath);
    for (size_t i = 0; i < args.len; i++)
        buf_printf(&cmdline, " \"%s\"", (const char *)args.items[i]);
    int rc = system(cmdline.data);
    free(cmdline.data);
    return rc;
#endif
}

/* ------------------------------------------------------------------ */
/* multi-file loading                                                  */
/* ------------------------------------------------------------------ */

typedef struct LoadCtx {
    PtrVec programs;   /* Program* in import order (deps first) */
    PtrVec visited;    /* const char* absolute-ish paths */
} LoadCtx;

/* the search path, in order: project libraries, the bundled std, the
 * importing file's own directory, $HPHP_PATH, then libraries compiled into
 * the hphp binary (so an installed compiler still has its stdlib). */
static Program *load_source_recursive(LoadCtx *ctx, const char *path, const char *src);

static bool lib_name_matches(const char *spec, const char *name) {
    const char *slash = strrchr(spec, '/');
    const char *bslash = strrchr(spec, '\\');
    const char *cut = slash > bslash ? slash : bslash;
    const char *base = cut ? cut + 1 : spec;
    return strcmp(base, name) == 0;
}

static Program *load_import(LoadCtx *ctx, const char *importer, const char *spec) {
    bool relative = spec[0] == '.' || spec[0] == '/' || spec[0] == '\\';
    if (relative) {
        char *cand = fmt("%s/%s", path_dir_of(importer), spec);
        char *src = read_file_soft(cand);
        if (src) return load_source_recursive(ctx, cand, src);
    } else {
        /* 1. lib/<name>   2. src/hphpc/std/<name>   3. next to the importer */
        char *cands[4];
        cands[0] = fmt("lib/%s", spec);
        cands[1] = fmt("src/hphpc/std/%s", spec);
        cands[2] = fmt("%s/%s", path_dir_of(importer), spec);
        cands[3] = pkg_import_candidate(spec);   /* installed packages */
        for (int i = 0; i < 4; i++) {
            char *src = read_file_soft(cands[i]);
            if (src) return load_source_recursive(ctx, cands[i], src);
        }
        /* 4. $HPHP_PATH (; or : separated), like PHP's include_path */
        const char *path = getenv("HPHP_PATH");
        if (path) {
            char *copy = xstrndup(path, strlen(path));
            for (char *p = copy, *nxt = copy; p && *p; p = nxt) {
                nxt = strpbrk(p, ";:");
                if (nxt) *nxt++ = 0;
                if (!*p) continue;
                char *cand = fmt("%s/%s", p, spec);
                char *src = read_file_soft(cand);
                if (src) return load_source_recursive(ctx, cand, src);
            }
        }
    }
    /* 5. a library shipped inside the compiler binary */
    for (size_t i = 0; i < hp_embed_libs_len; i++) {
        if (lib_name_matches(spec, hp_embed_libs[i].name)) {
            char *disp = fmt("<hphp-lib>/%s", hp_embed_libs[i].name);
            return load_source_recursive(ctx, disp, hp_embed_libs[i].src);
        }
    }
    return NULL;
}

static Program *load_source_recursive(LoadCtx *ctx, const char *path, const char *src) {
    const char *ipath = intern(path);
    for (size_t i = 0; i < ctx->visited.len; i++)
        if (ctx->visited.items[i] == ipath) return NULL;

    Program *prog = parse_program(src, path);
    ptrvec_push(&ctx->visited, (void *)ipath);

    /* imports first (depth-first, deps before user code) */
    for (size_t i = 0; i < prog->imports.len; i++) {
        Import *im = prog->imports.items[i];
        if (!load_import(ctx, path, im->path))
            fatal("%s: cannot import \"%s\"\n"
                  "  searched: lib/, src/hphpc/std/, %s/, $HPHP_PATH, built-in libraries",
                  path, im->path, path_dir_of(path));
    }
    ptrvec_push(&ctx->programs, prog);
    return prog;
}

static Program *load_file_recursive(LoadCtx *ctx, const char *path) {
    return load_source_recursive(ctx, path, read_file_or_die(path));
}

static void merge_programs(Program *out, PtrVec progs) {
    for (size_t i = 0; i < progs.len; i++) {
        Program *p = progs.items[i];
        for (size_t k = 0; k < p->decls.len; k++)
            ptrvec_push(&out->decls, p->decls.items[k]);
        /* top-level statements only from the root file (last one) */
    }
}

/* ------------------------------------------------------------------ */
/* package manager: hphp pkg <command>                                 */
/* ------------------------------------------------------------------ */

static int  do_check_file(const char *path);
static bool rm_rf_path(const char *path);   /* wrapper over pkgcore helper */

#define rm_rf rm_rf_path

#ifdef _WIN32
#include <direct.h>
#endif
static void pkg_read_manifest_of(const char *dir, PkgManifest *m) {
    char *mf = fmt("%s/" PKG_MANIFEST, dir);
    char *js = read_file_soft(mf);
    if (!js) fatal("%s: missing " PKG_MANIFEST, dir);
    *m = pkg_manifest_parse(js, dir);
    free(js); free(mf);
}

/* Pack the current project (or --dir) into <name>-<version>.hpx in cache/. */
static int pkg_cmd_pack(int argc, char **argv) {
    const char *dir = ".";
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
    PkgManifest m;
    pkg_read_manifest_of(dir, &m);
    char *cache = pkg_home_file("cache");
    char *out = fmt("%s/%s-%s.hpx", cache, m.name, m.version);
    pkg_pack_dir(dir, out);
    printf("packed %s (%s %s) -> %s\n", dir, m.name, m.version, out);
    return 0;
}

/* Install from a local .hpx file (no network needed). */
static int pkg_install_file(const char *hpx_path) {
    /* peek the manifest: extract to a temp pkg dir then read it */
    char *tmp = pkg_home_file("cache/.tmp-install");
    pkg_extract(hpx_path, tmp);
    PkgManifest m;
    pkg_read_manifest_of(tmp, &m);
    char *destdir = pkg_dir_of(m.name);
    rm_rf(destdir);
    mkdir_p(destdir);
    if (pkg_extract(hpx_path, destdir) <= 0)
        fatal("%s: empty package", hpx_path);
    /* sanity: entry must exist */
    char *entry = fmt("%s/%s", destdir, m.entry);
    if (!read_file_soft(entry))
        fatal("%s: package entry \"%s\" missing (rename it or set \"entry\" in hphp.json)",
              m.name, m.entry);
    pkg_registry_add(m.name, m.version);
    printf("installed %s %s -> %s\n", m.name, m.version, destdir);
    printf("import it with:  import \"%s.hphp\";\n", m.name);
    return 0;
}

/* Install from the registry over HTTP. */
static char *pkg_registry_url_or_default(void) {
    /* effective registry URL: $HPHP_REGISTRY, then the registry_url file,
     * then the public default */
    const char *env = getenv("HPHP_REGISTRY");
    if (env && *env) return xstrdup(env);
    char *from_file = pkg_registry_url();
    if (from_file) return from_file;
    return xstrdup(PKG_DEFAULT_REGISTRY);
}

static int pkg_install_registry(const char *name) {
    char *base = pkg_registry_url_or_default();
    char *url = fmt("%s/pkg/%s", base, name);
    size_t n;
    int status = 0;
    char *body = pkgnet_get(url, &n, &status);
    if (!body) return 2;   /* unreachable — caller may fall back */
    if (status == 404) {
        fprintf(stderr, "package not found on registry: %s\n", name);
        return 2;
    }
    if (status != 200) {
        fprintf(stderr, "registry error %d for %s\n", status, name);
        return 2;
    }
    /* save to cache, then install from the file */
    char *cache = pkg_home_file("cache");
    char *out = fmt("%s/%s.hpx", cache, name);
    FILE *f = fopen(out, "wb");
    if (!f) fatal("cannot write %s", out);
    fwrite(body, 1, n, f);
    fclose(f);
    free(body);
    return pkg_install_file(out);
}

/* Install a library that ships inside the hphp binary itself (lib/*.hphp is
 * compiled into the compiler at build time). Used as an offline fallback when
 * the registry is unreachable — the toolchain is self-contained like `go`.
 * Returns 0 on success, 1 when the name is not a bundled library. */
static int pkg_install_embedded(const char *name) {
    /* normalize "websocket.hphp" -> "websocket" */
    char stem[128];
    snprintf(stem, sizeof stem, "%s", name);
    size_t sl = strlen(stem);
    if (sl > 5 && !strcmp(stem + sl - 5, ".hphp")) stem[sl - 5] = 0;
    for (size_t i = 0; i < hp_embed_libs_len; i++) {
        /* embedded lib names are file names, e.g. "websocket.hphp" */
        const char *lname = hp_embed_libs[i].name;
        size_t ll = strlen(lname);
        if (ll > 5 && !strcmp(lname + ll - 5, ".hphp")) ll -= 5;
        if (strlen(stem) != ll || strncmp(stem, lname, ll) != 0) continue;
        /* stage the source into a temp dir, then pack it into a real .hpx so
         * the installed layout is identical to a registry install */
        char *stage = pkg_home_file("cache/.embedded");
        rm_rf(stage);
        mkdir_p(stage);
        char *src = fmt("%s/%s.hphp", stage, stem);
        FILE *f = fopen(src, "wb");
        if (!f) fatal("cannot write %s", src);
        fputs(hp_embed_libs[i].src, f);
        fclose(f);
        char *mf = fmt("%s/" PKG_MANIFEST, stage);
        f = fopen(mf, "wb");
        if (!f) fatal("cannot write %s", mf);
        fprintf(f,
            "{\n"
            "  \"name\": \"%s\",\n"
            "  \"version\": \"%s\",\n"
            "  \"entry\": \"%s.hphp\",\n"
            "  \"description\": \"Bundled library (installed from the hphp binary).\",\n"
            "  \"author\": \"HolyPHP\"\n"
            "}\n", stem, HPHP_VERSION, stem);
        fclose(f);
        char *hpx = fmt("%s/%s.hpx", stage, stem);
        pkg_pack_dir(stage, hpx);
        int rc = pkg_install_file(hpx);
        rm_rf(stage);
        return rc;
    }
    return 1;
}

static int pkg_cmd_install(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: hphp pkg install <name | ./file.hpx>\n"); return 1; }
    const char *what = argv[0];
    bool looks_file = strchr(what, '/') || strstr(what, ".hpx");
    if (looks_file) return pkg_install_file(what);
    int rc = pkg_install_registry(what);
    if (rc == 2) {
        /* registry unreachable / missing: fall back to the bundled copy */
        fprintf(stderr, "(registry unreachable — trying the libraries bundled in hphp)\n");
        rc = pkg_install_embedded(what);
        if (rc != 0)
            fatal("cannot install \"%s\": registry unreachable and not a bundled library", what);
        return 0;
    }
    return rc;
}

static int pkg_cmd_remove(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: hphp pkg remove <name>\n"); return 1; }
    if (!pkg_is_installed(argv[0])) { fprintf(stderr, "not installed: %s\n", argv[0]); return 1; }
    char *dir = pkg_dir_of(argv[0]);
    rm_rf(dir);
    pkg_registry_remove(argv[0]);
    printf("removed %s\n", argv[0]);
    return 0;
}

static int pkg_cmd_list(void) {
    size_t n;
    InstalledPkg *v = pkg_registry_read(&n);
    if (n == 0) { printf("no packages installed (try: hphp pkg install <name>)\n"); return 0; }
    printf("%-20s %s\n", "PACKAGE", "VERSION");
    for (size_t i = 0; i < n; i++)
        printf("%-20s %s\n", v[i].name, v[i].version);
    return 0;
}

static int pkg_cmd_search(int argc, char **argv) {
    const char *q = argc ? argv[0] : "";
    char *base = pkg_registry_url_or_default();
    {
        char *url = fmt("%s/search?q=%s", base, q);
        size_t n;
        int status = 0;
        char *body = pkgnet_get(url, &n, &status);
        if (body && status == 200) {
            fputs(body, stdout);
            return 0;
        }
        fprintf(stderr, "(registry unreachable, showing local packages)\n");
    }
    return pkg_search(q) > 0 ? 0 : 1;
}

/* init: create hphp.json skeleton in cwd */
static int pkg_cmd_init(int argc, char **argv) {
    /* name = directory name (sanitized) */
    char cwd[1024];
#ifdef _WIN32
    _getcwd(cwd, sizeof cwd);
#else
    if (!getcwd(cwd, sizeof cwd)) snprintf(cwd, sizeof cwd, ".");
#endif
    char *base = cwd;
    for (const char *p = cwd; *p; p++)
        if (*p == '/' || *p == '\\') base = (char *)(p + 1);
    char name[128]; size_t ni = 0;
    for (const char *p = base; *p && ni + 1 < sizeof name; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') name[ni++] = c;
    }
    name[ni] = 0;
    if (!ni) snprintf(name, sizeof name, "mypkg");
    const char *forced = NULL;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--name") && i + 1 < argc) forced = argv[++i];
    if (forced) snprintf(name, sizeof name, "%s", forced);
    if (!pkg_name_ok(name))
        fatal("package name must be lowercase letters, digits, _ (got \"%s\")", name);
    char *mf = fmt("./" PKG_MANIFEST);
    if (read_file_soft(mf)) { printf("%s already exists\n", mf); return 1; }
    FILE *f = fopen(mf, "wb");
    if (!f) fatal("cannot write %s", mf);
    fprintf(f,
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"entry\": \"%s.hphp\",\n"
        "  \"description\": \"A HolyPHP package.\",\n"
        "  \"author\": \"\"\n"
        "}\n", name, name);
    fclose(f);
    printf("created %s — add %s.hphp and run:\n", mf, name);
    printf("  hphp pkg check      (validate it imports)\n");
    printf("  hphp pkg pack       (build the .hpx)\n");
    return 0;
}

/* check: the package entry must type-check on its own */
static int pkg_cmd_check(void) {
    PkgManifest m;
    pkg_read_manifest_of(".", &m);
    char *entry = fmt("./%s", m.entry);
    if (!read_file_soft(entry))
        fatal("entry \"%s\" missing (create it or set \"entry\" in hphp.json)", m.entry);
    printf("checking %s %s (%s)... ", m.name, m.version, m.entry);
    fflush(stdout);
    return do_check_file(entry);   /* shared with `hphp check` */
}

/* publish: pack + POST the .hpx to the registry */
static int pkg_cmd_publish(int argc, char **argv) {
    const char *dir = ".";
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
    PkgManifest m;
    pkg_read_manifest_of(dir, &m);
    char *cache = pkg_home_file("cache");
    char *hpx = fmt("%s/%s-%s.hpx", cache, m.name, m.version);
    pkg_pack_dir(dir, hpx);
    char *base = pkg_registry_url_or_default();
    size_t hn;
    char *hpx_data = read_file_soft(hpx);
    hn = hpx_data ? strlen(hpx_data) : 0;
    if (!hpx_data) fatal("pack failed: %s", hpx);
    char *url = fmt("%s/publish", base);
    int status = 0;
    char *resp = pkgnet_post(url, "application/octet-stream", hpx_data, hn, &status);
    if (!resp) fatal("registry unreachable: %s", base);
    printf("%s", resp);
    return status == 200 ? 0 : 1;
}

static int pkg_cmd_registry(int argc, char **argv) {
    if (argc < 1) {
        const char *env = getenv("HPHP_REGISTRY");
        char *from_file = pkg_registry_url();
        char *eff = pkg_registry_url_or_default();
        printf("registry: %s\n", eff);
        if (env && *env) {
            printf("  (from $HPHP_REGISTRY; override with: hphp pkg registry <url>)\n");
        } else if (from_file) {
            printf("  (change it with: hphp pkg registry <url>)\n");
        } else {
            printf("  (default — pin yours with: hphp pkg registry <url>\n"
                   "           or:        export HPHP_REGISTRY=<url>)\n");
        }
        return 0;
    }
    if (strncmp(argv[0], "http://", 7) != 0)
        fatal("registry URL must start with http:// (TLS comes later)");
    char *f = pkg_home_file(PKG_REGISTRY_FILE);
    FILE *fp = fopen(f, "wb");
    if (!fp) fatal("cannot write %s", f);
    fputs(argv[0], fp);
    fclose(fp);
    printf("registry set to %s\n", argv[0]);
    return 0;
}

static int pkg_cmd_home(void) {
    char *h = pkg_home();
    printf("%s\n", h);
    return 0;
}

static int pkg_command(int argc, char **argv) {
    if (argc < 1 || !strcmp(argv[0], "help") || !strcmp(argv[0], "--help")) {
        printf(
            "usage: hphp pkg <command>\n"
            "\n"
            "  init               create an hphp.json skeleton in this folder\n"
            "  check              type-check this package's entry\n"
            "  pack               build <name>-<version>.hpx from this folder\n"
            "  publish            pack + upload to the registry\n"
            "  install <what>     install a package (registry name or ./file.hpx)\n"
            "  remove <name>      uninstall\n"
            "  list               list installed packages\n"
            "  search [q]         search (registry when reachable, else local)\n"
            "  registry [url]     show or set the registry URL\n"
            "  home               print the package home directory\n");
        return argc < 1 ? 1 : 0;
    }
    const char *sub = argv[0];
    if (!strcmp(sub, "init"))     return pkg_cmd_init(argc - 1, argv + 1);
    if (!strcmp(sub, "check"))    return pkg_cmd_check();
    if (!strcmp(sub, "pack"))     return pkg_cmd_pack(argc - 1, argv + 1);
    if (!strcmp(sub, "publish"))  return pkg_cmd_publish(argc - 1, argv + 1);
    if (!strcmp(sub, "install"))  return pkg_cmd_install(argc - 1, argv + 1);
    if (!strcmp(sub, "remove"))   return pkg_cmd_remove(argc - 1, argv + 1);
    if (!strcmp(sub, "uninstall"))return pkg_cmd_remove(argc - 1, argv + 1);
    if (!strcmp(sub, "list"))     return pkg_cmd_list();
    if (!strcmp(sub, "search"))   return pkg_cmd_search(argc - 1, argv + 1);
    if (!strcmp(sub, "registry")) return pkg_cmd_registry(argc - 1, argv + 1);
    if (!strcmp(sub, "home"))     return pkg_cmd_home();
    fprintf(stderr, "hphp pkg: unknown command \"%s\" (try: hphp pkg help)\n", sub);
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

/* check one file on its own (used by `hphp check` and `hphp pkg check`).
 * Returns process exit code. */
static bool rm_rf_path(const char *path) {
    /* implemented by pkgcore via this trampoline: pkgcore's rm_rf is static,
     * so expose removal through the public API (pkg_cmd_remove uses it too). */
    extern bool pkg_remove_tree(const char *path);   /* pkgcore.c */
    return pkg_remove_tree(path);
}

int do_check_file(const char *path) {
    LoadCtx c = {0};
    load_file_recursive(&c, path);
    if (c.programs.len == 0) fatal("no programs loaded");
    Program *w = xcalloc(1, sizeof(Program));
    w->file = intern(path);
    merge_programs(w, c.programs);
    Program *r = c.programs.items[c.programs.len - 1];
    w->stmts = r->stmts;
    builtins_init();
    sema_run(w);
    if (!sema_result_ok()) {
        fprintf(stderr, "hphp: compilation failed\n");
        return 1;
    }
    fprintf(stderr, "%s: OK (types and borrows check)\n", path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        fprintf(stderr,
                "HolyPHP %s — a low-level, PHP-flavored compiled language\n"
                "\n"
                "usage: hphp <file.hphp>               run a HolyPHP program\n"
                "       hphp run <file.hphp>           run a HolyPHP program\n"
                "       hphp build <file.hphp> [-o app]  build a native executable\n"
                "       hphp check <file.hphp>         type- & borrow-check only\n"
                "       hphp emit <file.hphp>          inspect the internal representation\n"
                "       hphp pkg <command> [args]      package manager (hphp pkg help)\n"
                "       hphp install <package>         install a package (shorthand)\n"
                "       hphp version                   print version\n",
                HPHP_VERSION);
        return argc < 2 ? 1 : 0;
    }
    const char *cmd = argv[1];
    const char *file = NULL;
    const char *outname = NULL;
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 ||
        strcmp(cmd, "-v") == 0) {
        printf("HolyPHP %s\n", HPHP_VERSION);
        return 0;
    }
    if (strcmp(cmd, "pkg") == 0 || strcmp(cmd, "package") == 0) {
        return pkg_command(argc - 2, argv + 2);   /* consumes argv[2..] */
    }
    /* hphp install <pkg> — the common case, same as `hphp pkg install` */
    if (strcmp(cmd, "install") == 0) {
        return pkg_cmd_install(argc - 2, argv + 2);
    }
    /* default command is "run" — hphp file.hphp just works */
    if (strcmp(cmd, "run") != 0 && strcmp(cmd, "build") != 0 &&
        strcmp(cmd, "emit") != 0 && strcmp(cmd, "check") != 0) {
        file = cmd;
        cmd = "run";
    }
    PtrVec prog_args = {0};
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outname = argv[++i];
        else if (!file) file = argv[i];
        else ptrvec_push(&prog_args, argv[i]);
    }
    if (!file) { fprintf(stderr, "hphp: no input file\n"); return 1; }
    bool do_emit = strcmp(cmd, "emit") == 0;
    bool do_check = strcmp(cmd, "check") == 0;

    LoadCtx ctx = {0};
    load_file_recursive(&ctx, file);
    if (ctx.programs.len == 0) fatal("no programs loaded");

    Program *whole = xcalloc(1, sizeof(Program));
    whole->file = intern(file);
    merge_programs(whole, ctx.programs);
    /* root file = last loaded (it imports its deps first) */
    Program *root = ctx.programs.items[ctx.programs.len - 1];
    whole->stmts = root->stmts;

    builtins_init();
    sema_run(whole);
    if (!sema_result_ok()) {
        fprintf(stderr, "hphp: compilation failed\n");
        return 1;
    }
    if (do_check) {
        fprintf(stderr, "%s: OK (types and borrows check)\n", file);
        return 0;
    }

    Buf out;
    buf_init(&out);
    codegen_emit(whole, &out);
    if (do_emit) {
        fwrite(out.data, 1, out.len, stdout);
        return 0;
    }

    /* ---- backend: everything happens in hphp's private build dir ---- */
    char *self = find_compiler(argv[0]);
    const char *wd = work_dir();
    extract_runtime(wd, self);
    char *rt = path_join(wd, "rt");

    /* generated C goes into the private dir — never the user's project */
    char *cpath = path_join(wd, "prog.c");
    FILE *cf = fopen(cpath, "wb");
    if (!cf) fatal("hphp: cannot write build file %s", cpath);
    fwrite(out.data, 1, out.len, cf);
    fclose(cf);

    /* executable destination: -o path, else next to the source file.
     * Always absolute so the final rename and spawn work from inside wd. */
#ifdef _WIN32
    char cwd_buf[2048];
    GetCurrentDirectoryA(sizeof cwd_buf, cwd_buf);
    const char *cwd = cwd_buf;
#else
    char *cwd = getcwd(NULL, 0);
#endif
    char *exepath;
    if (outname) {
        bool absolute = outname[0] == '/' || outname[0] == '\\' ||
                        (outname[1] == ':' &&
                         (outname[2] == '/' || outname[2] == '\\'));
        if (absolute) {
            exepath = xstrdup(outname);
        } else {
            exepath = fmt("%s/%s", cwd, outname);
        }
#ifdef _WIN32
        if (!strchr(path_file_name(exepath), '.'))
            exepath = fmt("%s.exe", exepath); /* runnable name on Windows */
#endif
    } else {
        const char *base = path_file_name(file);
        const char *stem = "prog";
        const char *dot = strrchr(base, '.');
        if (dot && dot != base) stem = xstrndup(base, dot - base);
        const char *slash = strrchr(file, '/'), *bslash = strrchr(file, '\\');
        const char *cut = slash > bslash ? slash : bslash;
        if (cut) {
            char *dir = xstrndup(file, cut - file);
            exepath = fmt("%s/%s", dir, stem);
#ifdef _WIN32
            exepath = fmt("%s.exe", exepath);
#endif
        } else {
            exepath = fmt("%s/%s", cwd, stem);
#ifdef _WIN32
            exepath = fmt("%s.exe", exepath);
#endif
        }
    }

    if (cc_build_in_dir(wd, rt, cpath, exepath) != 0) {
        fprintf(stderr, "hphp: backend failed to produce an executable\n");
        return 1;
    }

    if (strcmp(cmd, "build") == 0) {
        fprintf(stderr, "hphp: built %s\n", exepath);
        return 0;
    }

    /* run: execute the freshly built binary (console I/O passes through) */
    int rc = spawn_wait(exepath, prog_args);
    if (!getenv("HPHP_KEEP_BIN")) remove_quiet(exepath); /* ephemeral run */
    return rc;
}
