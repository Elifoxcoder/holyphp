/* hphp_rt.h — HolyPHP runtime interface.
 *
 * Every generated program includes this header and links against
 * libhphp.a (the runtime implemented in hphp_rt.c + hphp_std.c).
 */
#ifndef HPHP_RT_H
#define HPHP_RT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- string builder (runtime-internal helper) ---------- */
typedef struct {
    char *data;
    size_t len, cap;
} Buf;
void hbuf_init(Buf *b);
void hbuf_putc(Buf *b, char c);
void hbuf_write(Buf *b, const char *s, size_t n);
void hbuf_puts(Buf *b, const char *s);
void hbuf_printf(Buf *b, const char *fmt, ...);
char *hbuf_take(Buf *b); /* NUL-terminated, transfers ownership */
#define buf_init  hbuf_init
#define buf_putc  hbuf_putc
#define buf_write hbuf_write
#define buf_puts  hbuf_puts
#define buf_printf hbuf_printf
#define buf_take  hbuf_take

/* ---------- forward types ---------- */
typedef struct hstr hstr;
typedef struct harr harr;
typedef struct hobj hobj;
typedef struct hclosure hclosure;
typedef struct hval hval;

/* ---------- strings: length-prefixed, refcounted ---------- */
struct hstr {
    size_t len;         /* byte length (PHP-style byte strings) */
    size_t cap;         /* allocated bytes incl. NUL; >= len+1, 0 for literals */
    uint32_t hash;      /* 0 = not computed */
    uint32_t rc;
    char data[];        /* NUL-terminated for C interop */
};

hstr *hp_str_lit(const char *cstr);
hstr *hp_str_new(const char *bytes, size_t len);
hstr *hp_str_copy(hstr *s);
void hp_str_free(hstr *s);
bool hp_str_eq(hstr *a, hstr *b);
int hp_str_cmp(hstr *a, hstr *b);
hstr *hp_str_concat2(hstr *a, hstr *b);
hstr *hp_str_concat_n(int n, const hval *vals); /* array of hvals */
hstr *hp_str_concat_v(hval a, hval b);          /* PHP '.' on any two values */
hstr *hp_str_slice(hstr *s, int64_t start, int64_t end);
hstr *hp_str_char_at(hstr *s, int64_t idx);
int64_t hp_str_len(hstr *s);
int64_t hp_str_index(hstr *haystack, hstr *needle);
hstr *hp_str_from_int(int64_t v);
hstr *hp_str_from_double(double v);
hstr *hp_str_from_bool(bool b);
int64_t hp_str_to_int(hstr *s);
double hp_str_to_double(hstr *s);
bool hp_str_is_numeric(hstr *s); /* PHP is_numeric() on a string value */
uint32_t hp_str_hash(hstr *s);
hstr *hp_str_toupper(hstr *s);
hstr *hp_str_tolower(hstr *s);
hstr *hp_str_repeat(hstr *s, int64_t n);
hstr *hp_str_replace(hstr *subject, hstr *search, hstr *replace);
hstr *hp_str_trim(hstr *s);
hstr *hp_str_reverse(hstr *s);

/* ---------- dynamic value (PHP zval analogue) ---------- */
typedef enum {
    HV_NULL = 0, HV_INT, HV_FLOAT, HV_BOOL, HV_STR, HV_ARR, HV_OBJ, HV_CLO
} HKind;

struct hval {
    HKind tag;
    union {
        int64_t i;
        double f;
        bool b;
        hstr *s;
        harr *a;
        void *p;
        hclosure *c;
    } u;
};

hval hp_of_int(int64_t i);
hval hp_of_float(double f);
hval hp_of_bool(bool b);
hval hp_of_str(hstr *s);
hval hp_of_arr(harr *a);
hval hp_of_ptr(void *p);
hval hp_of_clo(hclosure *c);
static inline hval hp_hv_id(hval v) { return v; }
#define hv(x) _Generic((x), \
    hval: hp_hv_id, \
    int: hp_of_int, \
    long long: hp_of_int, \
    unsigned long long: hp_of_int, \
    long: hp_of_int, \
    unsigned int: hp_of_int, \
    double: hp_of_float, \
    bool: hp_of_bool, \
    hstr*: hp_of_str, \
    harr*: hp_of_arr, \
    hclosure*: hp_of_clo, \
    default: hp_of_ptr)(x)

extern hval hp_null;
hstr *hp_null_str(void);
harr *hp_null_arr(void);
void hp_val_free(hval v);
void hp_val_rc_inc(hval v);
void hp_val_rc_dec(hval v);
const char *hp_kind_name(HKind k);
hstr *hp_val_to_str(hval v);       /* PHP-style cast to string */
int64_t hp_val_to_int(hval v);
double hp_val_to_float(hval v);
bool hp_val_to_bool(hval v);
bool hp_val_eq(hval a, hval b);    /* == */
int64_t hp_val_cmp(hval a, hval b);/* <=> */
HKind hp_tag(hval v);

/* ---------- arrays / maps (COW) ---------- */
struct harr {
    size_t len, cap;
    uint32_t rc;
    bool is_map;
    int64_t next_index;
    hval *vals;
    hval *keys;         /* for maps; NULL for plain arrays */
    /* ---- open-addressing hash index for maps (lazily built) ----
     * slots store value-slot+1; 0 = empty. -1u marks a deleted slot.
     * Rebuilt whenever len changes beyond the incremental budget. */
    uint32_t *index;    /* hash -> slot+1 table; NULL = not built */
    uint32_t icap;      /* power of two, or 0 */
    uint32_t ibudget;   /* inserts until full rebuild (tombstone pressure) */
};

harr *hp_arr_new(void);
harr *hp_arr_of(int n, const hval *vals);      /* counted hval array */
harr *hp_map_of(int n, const hval *kvs);       /* k,v,k,v... counted */
harr *hp_arr_clone(harr *a);   /* deep copy */
harr *hp_arr_cow(harr *a);     /* returns a writable version (COW) */
void hp_arr_push(harr *a, hval v);
hval hp_arr_push_v(harr *a, hval v);   /* PHP $a[] = v; returns the array ptr as hval */
harr *hp_val_to_arr(hval v);               /* PHP (array) cast */
hval hp_arr_push_vv(hval container, hval v); /* append through a boxed (hval) container */
hval hp_val_clone_v(hval v);           /* value-semantics copy: deep-clones arrays */
int64_t hp_pow_i(int64_t a, int64_t b); /* integer pow for ** on ints */
hval hp_arr_pop(harr *a);
hval hp_arr_shift(harr *a);
void hp_arr_unshift(harr *a, hval v);
hval hp_arr_get(harr *a, hval idx);
hval hp_arr_get_v(hval container, hval idx); /* PHP-style: index any value */
void hp_arr_set_v(hval container, hval idx, hval v); /* set on hval container */
void hp_arr_set(harr *a, hval idx, hval v);
bool hp_arr_has(harr *a, hval key);
void hp_arr_unset(harr *a, hval key);
int64_t hp_arr_len(harr *a);
harr *hp_arr_slice(harr *a, int64_t start, int64_t end);
harr *hp_arr_keys(harr *a);
harr *hp_arr_values(harr *a);
harr *hp_arr_merge(harr *a, harr *b);
harr *hp_arr_reverse(harr *a);
bool hp_arr_in(harr *a, hval v);
harr *hp_arr_sort(harr *a);
harr *hp_arr_rsort(harr *a);
harr *hp_map_sort_by_key(harr *a);
hval hp_arr_sum(harr *a);
harr *hp_arr_unique(harr *a);
harr *hp_range(int64_t lo, int64_t hi);
void hp_arr_free(harr *a);

/* foreach protocol */
typedef struct {
    harr *a;
    hstr *s;
    size_t pos;
    int64_t i;
    hval k;
    bool is_str;
} hp_foreach_ctx;
hp_foreach_ctx hp_arr_iter(harr *a);
hp_foreach_ctx hp_map_iter(harr *a);
hp_foreach_ctx hp_val_iter(hval v);   /* foreach over a dynamic value */
hp_foreach_ctx hp_str_iter(hstr *s);
extern hval hpv_tmp;
bool hp_foreach_next(hp_foreach_ctx *ctx, hval *out);

/* ---------- dynamic operators ---------- */
hval hp_add(hval a, hval b);
hval hp_sub(hval a, hval b);
hval hp_mul(hval a, hval b);
hval hpbi_scalar_sub(hval a, hval b);
hval hpbi_scalar_mul(hval a, hval b);
hval hp_divv(hval a, hval b);
bool hp_streq(hval a, hval b);
bool hp_ident_eq(hval a, hval b);
hval hpbi_stream_socket_accept2(hval srv, hval timeout_ms);
bool hp_lt(hval a, hval b);
bool hp_gt(hval a, hval b);
bool hp_le(hval a, hval b);
bool hp_ge(hval a, hval b);
/* scalar fast paths for generated code */
static inline bool hp_le_i(int64_t a, int64_t b) { return a <= b; }
static inline bool hp_ge_i(int64_t a, int64_t b) { return a >= b; }
static inline bool hp_lt_i(int64_t a, int64_t b) { return a < b; }
static inline bool hp_gt_i(int64_t a, int64_t b) { return a > b; }
/* PHP-semantics modulo, inline so hot loops stay call-free (gcc can turn
 * `x % 7` into a magic-number multiply); throws "Modulo by zero" like hp_mod */
void hp_throw_str(const char *msg);   /* fwd: real decl below */
static inline int64_t hp_mod_i(int64_t a, int64_t b) {
    if (b == 0) hp_throw_str("Modulo by zero");
    return a % b;
}
static inline bool hp_le_f(double a, double b) { return a <= b; }
static inline bool hp_ge_f(double a, double b) { return a >= b; }
static inline bool hp_lt_f(double a, double b) { return a < b; }
static inline bool hp_gt_f(double a, double b) { return a > b; }
static inline bool hp_eq_f(double a, double b) { return a == b; }
int64_t hp_cmp(hval a, hval b);   /* spaceship on hvals */
int64_t hp_mod(int64_t a, int64_t b);
hval hp_powv(hval a, hval b);
hval hp_nullcoal(hval a, hval b);
hval hp_match_any(hval subj, hval *pats, size_t n);
hval hp_match_no_default(void);
/* inline so gcc folds the zero-check away for literal divisors
 * (e.g. `$x / 2` becomes one divsd instruction) */
static inline double hp_div(double a, double b) {
    if (b == 0.0) hp_throw_str("Division by zero");
    return a / b;
}

/* ---------- exceptions ---------- */
typedef struct hp_try_frame {
    jmp_buf jmp;
    struct hp_try_frame *prev;
    bool caught;
} hp_try_frame;
extern hp_try_frame *hp_cur_try;
extern hval hp_exception;
void hp_try_push(hp_try_frame *f);
void hp_try_pop(hp_try_frame *f);
void hp_throw(hval v);
void hp_throw_str(const char *msg);
hstr *hp_exception_msg(void);

/* try/catch runner: setjmp lives HERE so generated functions never
 * contain setjmp — no volatile locals, no register-clobber UB at -O2.
 * body_fn(env, hp_null) runs the try body; on a throw catch_fn(env, exc)
 * runs the handler. Returning a non-null hval from the handler means
 * "unhandled": the value comes back here so the caller can run its finally
 * block before rethrowing with hp_throw(). */
typedef hval (*hp_try_fn)(void *env, hval exv);
hval hp_try_run(hp_try_fn body_fn, hp_try_fn catch_fn, void *env);

/* ---------- closures ---------- */
typedef hval (*hfn_t)(void);
struct hclosure {
    hfn_t fn;
    uint32_t rc;
    size_t envsz;
    /* environment bytes follow */
};
hclosure *hpclosure_new(void *env, size_t envsz, void *fn);
void hpclosure_free(hclosure *c);
void *hp_closure_env(void);                       /* env of the running closure */
hval hp_closure_call_arr(hclosure *c, hval *args, int nargs);
hval hp_closure_call(hclosure *c, int nargs, const hval *args);
hval hp_call_value(hval fn, int nargs, const hval *args);

/* ---------- smart pointer support ---------- */
void hp_rc_inc(void *p);
void hp_rc_dec(void *p);

/* ---------- objects ---------- */
struct hobj {
    uint32_t rc;
};

void *hp_alloc(size_t n);
void hp_echo(hval v);              /* PHP echo: print value */

/* ---------- builtin function entry points (hphp_std.c) ---------- */
hval hpbi_strlen(hval s);
hval hpbi_count(hval a);
hval hpbi_sizeof(hval a);
hval hpbi_strtoupper(hval s);
hval hpbi_strtolower(hval s);
hval hpbi_ucfirst(hval s);
hval hpbi_lcfirst(hval s);
hval hpbi_trim(hval s);
hval hpbi_ltrim(hval s, hval chars);
hval hpbi_rtrim(hval s, hval chars);
hval hpbi_chop(hval s);
hval hpbi_strrev(hval s);
hval hpbi_str_repeat(hval s, hval n);
hval hpbi_str_pad(hval s, hval n, hval pad);
hval hpbi_str_replace(hval subject, hval search, hval replace);
hval hpbi_substr(hval s, hval start, hval len);
hval hpbi_strstr(hval h, hval n);
hval hpbi_strchr(hval h, hval n);
hval hpbi_strpos(hval h, hval n, hval off);
hval hpbi_str_contains(hval h, hval n);
hval hpbi_str_starts_with(hval h, hval n);
hval hpbi_str_ends_with(hval h, hval n);
hval hpbi_strcmp(hval a, hval b);
hval hpbi_strncmp(hval a, hval b, hval n);
hval hpbi_strcasecmp(hval a, hval b);
hval hpbi_strncasecmp(hval a, hval b, hval n);
hval hpbi_explode(hval sep, hval s);
hval hpbi_split(hval sep, hval s);
hval hpbi_implode(hval glue, hval arr);
hval hpbi_join(hval glue, hval arr);
hval hpbi_nl2br(hval s);
hval hpbi_number_format(hval n, hval dec);
hval hpbi_unset(harr *a, hval key);
hval hpbi_json_encode(hval v);
hval hpbi_json_decode(hval s);
hval hpbi_md5(hval s, hval raw_output);   /* real MD5; raw=true -> 16 bytes */
hval hpbi_sha1(hval s, hval raw_output);  /* real SHA-1; raw=true -> 20 bytes */

/* sockets (see hphp_std.c) — handles are integers, non-blocking by default */
hval hpbi_stream_socket_server(hval addr);       /* "tcp://host:port" */
hval hpbi_stream_socket_client(hval addr);
hval hpbi_stream_socket_accept(hval srv);
hval hpbi_stream_set_blocking(hval s, hval block);
hval hpbi_stream_recv(hval s, hval max);
hval hpbi_stream_send(hval s, hval data);
hval hpbi_stream_ready(hval handles, hval timeout_ms);
hval hpbi_stream_eof(hval s);
hval hpbi_stream_peer(hval s);
hval hpbi_stream_close(hval s);
hval hpbi_crc32(hval s);
hval hpbi_base64_encode(hval s);
hval hpbi_base64_decode(hval s);
hval hpbi_urlencode(hval s);
hval hpbi_urldecode(hval s);
hval hpbi_htmlentities(hval s);
hval hpbi_htmlspecialchars(hval s);
hval hpbi_sprintf(hval fmt, hval args);
hval hpbi_printf(hval fmt, hval args);
hval hpbi_ord(hval s);
hval hpbi_chr(hval n);
hval hpbi_bin2hex(hval s);
hval hpbi_hex2bin(hval s);
hval hpbi_str_split(hval s, hval n);
hval hpbi_ucwords(hval s);
hval hpbi_wordwrap(hval s, hval w);
hval hpbi_similar_text(hval a, hval b);
hval hpbi_levenshtein(hval a, hval b);
hval hpbi_array_keys(hval a);
hval hpbi_array_values(hval a);
hval hpbi_array_merge(hval a, hval b);
hval hpbi_array_slice(hval a, hval off, hval len);
hval hpbi_array_reverse(hval a);
hval hpbi_array_sum(hval a);
hval hpbi_array_product(hval a);
hval hpbi_array_unique(hval a);
hval hpbi_in_array(hval needle, hval hay);
hval hpbi_array_search(hval needle, hval hay);
hval hpbi_array_key_exists(hval k, hval a);
hval hpbi_isset(hval v);
hval hpbi_range(hval lo, hval hi);
hval hpbi_array_map(hval fn, hval a);
hval hpbi_array_filter(hval a, hval fn);
hval hpbi_array_reduce(hval a, hval fn, hval init);
hval hpbi_array_flip(hval a);
hval hpbi_array_fill(hval start, hval n, hval v);
hval hpbi_array_combine(hval keys, hval vals);
hval hpbi_array_diff(hval a, hval b);
hval hpbi_array_intersect(hval a, hval b);
hval hpbi_array_push(hval a, hval v);
hval hpbi_array_pop(hval a);
hval hpbi_end(hval a);
hval hpbi_reset(hval a);
hval hpbi_array_shift(hval a);
hval hpbi_array_unshift(hval a, hval v);
hval hpbi_array_splice(hval a, hval off, hval len);
hval hpbi_shuffle(hval a);
hval hpbi_sort(hval a);
hval hpbi_rsort(hval a);
hval hpbi_ksort(hval a);
hval hpbi_krsort(hval a);
hval hpbi_asort(hval a);
hval hpbi_max(hval a);
hval hpbi_min(hval a);
hval hpbi_abs(hval v);
hval hpbi_round(hval v, hval prec);
hval hpbi_floor(hval v);
hval hpbi_ceil(hval v);
hval hpbi_sqrt(hval v);
hval hpbi_pow(hval a, hval b);
hval hpbi_intdiv(hval a, hval b);
hval hpbi_fmod(hval a, hval b);
hval hpbi_sin(hval v);
hval hpbi_cos(hval v);
hval hpbi_tan(hval v);
hval hpbi_atan(hval v);
hval hpbi_atan2(hval a, hval b);
hval hpbi_asin(hval v);
hval hpbi_acos(hval v);
hval hpbi_log(hval v);
hval hpbi_log2(hval v);
hval hpbi_log10(hval v);
hval hpbi_exp(hval v);
hval hpbi_is_nan(hval v);
hval hpbi_is_finite(hval v);
hval hpbi_is_infinite(hval v);
hval hpbi_pi(void);
hval hpbi_deg2rad(hval v);
hval hpbi_rad2deg(hval v);
hval hpbi_hypot(hval a, hval b);
hval hpbi_is_int(hval v);
hval hpbi_is_integer(hval v);
hval hpbi_is_float(hval v);
hval hpbi_is_string(hval v);
hval hpbi_is_bool(hval v);
hval hpbi_is_array(hval v);
hval hpbi_is_null(hval v);
hval hpbi_is_numeric(hval v);
hval hpbi_intval(hval v);
hval hpbi_floatval(hval v);
hval hpbi_doubleval(hval v);
hval hpbi_strval(hval v);
hval hpbi_boolval(hval v);
hval hpbi_gettype(hval v);
hval hpbi_var_dump(hval v);
hval hpbi_print_r(hval v);
hval hpbi_var_export(hval v);
hval hpbi_serialize(hval v);
hval hpbi_unserialize(hval s);
hval hpbi_readline(hval prompt);
hval hpbi_rand(hval mn, hval mx);
hval hpbi_mt_rand(hval mn, hval mx);
hval hpbi_srand(hval seed);
hval hpbi_mt_srand(hval seed);
hval hpbi_random_int(hval mn, hval mx);
hval hpbi_fgetc(hval stream);

/* filesystem helpers */
hval hpbi_getcwd(void);
hval hpbi_file_exists(hval path);
hval hpbi_is_dir(hval path);
hval hpbi_filesize(hval path);
hval hpbi_mkdir(hval path);
hval hpbi_rmdir(hval path);
hval hpbi_unlink(hval path);
hval hpbi_rename(hval from, hval to);
hval hpbi_copy(hval from, hval to);
hval hpbi_fgets(hval path);
hval hpbi_fwrite(hval path, hval data);
hval hpbi_fopen(hval path, hval mode);
hval hpbi_fclose(hval path);
hval hpbi_scandir(hval path);

/* number base conversion */
hval hpbi_decbin(hval n);
hval hpbi_decoct(hval n);
hval hpbi_dechex(hval n);
hval hpbi_bindec(hval s);
hval hpbi_octdec(hval s);
hval hpbi_hexdec(hval s);
hval hpbi_file_get_contents(hval path);
hval hpbi_file_put_contents(hval path, hval data);
hval hpbi_getenv(hval name);
hval hpbi_putenv(hval kv);
hval hpbi_php_uname(void);
hval hpbi_phpversion(void);
hval hpbi_php_sapi_name(void);
hval hpbi_microtime(void);
hval hpbi_hrtime(hval as_number);
hval hpbi_memory_get_usage(void);
hval hpbi_memory_get_peak_usage(void);
hval hpbi_gc_collect_cycles(void);
hval hpbi_date(hval fmt);
hval hpbi_time(void);
hval hpbi_usleep(hval us);
hval hpbi_sleep(hval s);
hval hpbi_exit(hval code);
hval hpbi_die(hval code);

/* ---------- lifecycle ---------- */
void hp_init(void);
void hp_shutdown(void);

/* PHP superglobal support + randomness */
harr *hp_argv_from_c(int argc, char **argv);
extern int hp_argc_global;
int64_t hp_rand(void);
int64_t hp_rand_range(int64_t min, int64_t max);
double hp_mt_rand(void);

/* ---------- UI layer (Win32; no-op stubs elsewhere) ---------- */
/* Controls live in a runtime-side table; hval slots carry the int handle. */
#define HPUI_WINDOW 1
#define HPUI_BUTTON 2
#define HPUI_LABEL 3
#define HPUI_INPUT 4
#define HPUI_CHECKBOX 5
#define HPUI_LIST 6
#define HPUI_COMBO 7
#define HPUI_PROGRESS 8
#define HPUI_SLIDER 9
#define HPUI_GROUP 10
#define HPUI_TAB 11
#define HPUI_PANEL 12
#define HPUI_MENU 13
#define HPUI_MENUITEM 14
#define HPUI_PICTURE 15

void hpui_init(void);
int64_t hpui_dispatch(int ms);            /* pump events up to ms, 0 = block */
void hpui_run_main(void);                 /* blocking GetMessage loop */
void hpui_quit(void);                     /* post WM_QUIT */

int64_t hpui_window_new(const char *title, int x, int y, int w, int h);
int64_t hpui_window_show(int64_t win, bool visible);
int64_t hpui_window_title(int64_t win, const char *title);
int64_t hpui_window_size(int64_t win, int w, int h);
int64_t hpui_window_pos(int64_t win, int x, int y);
int64_t hpui_window_close(int64_t win);
bool hpui_window_alive(int64_t win);

int64_t hpui_ctrl_new(int64_t kind, int64_t parent, const char *text,
                      int x, int y, int w, int h);
int64_t hpui_ctrl_set_text(int64_t h, const char *text);
int64_t hpui_ctrl_text(int64_t h, hstr **out);          /* returns 0/-1 */
int64_t hpui_ctrl_show(int64_t h, bool visible);
int64_t hpui_ctrl_enable(int64_t h, bool enabled);
int64_t hpui_ctrl_move(int64_t h, int x, int y, int w, int hgt);
int64_t hpui_ctrl_focus(int64_t h);
int64_t hpui_list_add(int64_t h, const char *item);
int64_t hpui_list_clear(int64_t h);
int64_t hpui_list_remove(int64_t h, int64_t index);
int64_t hpui_list_count(int64_t h, int64_t *out);
int64_t hpui_list_selected(int64_t h, int64_t *out);
int64_t hpui_list_select(int64_t h, int64_t index);
int64_t hpui_list_text(int64_t h, int64_t index, hstr **out);
int64_t hpui_check_get(int64_t h, bool *out);
int64_t hpui_check_set(int64_t h, bool on);
int64_t hpui_progress_set(int64_t h, int64_t pct);
int64_t hpui_slider_get(int64_t h, int64_t *out);
int64_t hpui_slider_set(int64_t h, int64_t pos);
int64_t hpui_ctrl_checked(int64_t h);                    /* menu item check */
int64_t hpui_menu_new(int64_t win, const char *label);
int64_t hpui_menu_item(int64_t menu, const char *label, int64_t id);
int64_t hpui_menu_sep(int64_t menu);
int64_t hpui_ctrl_set_bg(int64_t h, int64_t rgb);
int64_t hpui_ctrl_set_fg(int64_t h, int64_t rgb);
int64_t hpui_ctrl_font(int64_t h, int64_t size, bool bold);
int64_t hpui_picture_load(int64_t h, const char *path);

/* events: cb is a closure; id identifies which control fired */
int64_t hpui_on_event(int64_t handle, int64_t evtype, hval cb);
/* timer: id > 0; ms = 0 cancels */
int64_t hpui_timer(int64_t ms, hval cb);
/* message box; type 0=info 1=warn 2=error 3=question; returns 1/0 for yes/no */
int64_t hpui_msgbox(int64_t win, const char *title, const char *text, int type);
/* file/folder pickers; return hp_null on cancel */
hval hpui_open_file(int64_t win, const char *filter);
hval hpui_save_file(int64_t win, const char *filter);
hval hpui_pick_folder(int64_t win);
hval hpui_color_pick(int64_t win, int64_t init_rgb);
/* clipboard */
int64_t ui_clip_set(const char *text);
hval ui_clip_get(void);

/* UI builtins (implemented in hphp_std.c) */
hval hpbi_ui_dispatch(hval ms);
hval hpbi_ui_run_main(void);
hval hpbi_ui_quit(void);
hval hpbi_ui_window(hval title, hval x, hval y, hval w, hval hgt);
hval hpbi_ui_show(hval h, hval vis);
hval hpbi_ui_title(hval h, hval t);
hval hpbi_ui_size(hval h, hval w, hval hgt);
hval hpbi_ui_pos(hval h, hval x, hval y);
hval hpbi_ui_close(hval h);
hval hpbi_ui_alive(hval h);
hval hpbi_ui_add(hval parent, hval kind, hval text, hval x, hval y, hval w, hval hgt);
hval hpbi_ui_set(hval h, hval text);
hval hpbi_ui_get(hval h);
hval hpbi_ui_enable(hval h, hval on);
hval hpbi_ui_move(hval h, hval x, hval y, hval w, hval hgt);
hval hpbi_ui_focus(hval h);
hval hpbi_ui_items(hval h, hval item);
hval hpbi_ui_remove(hval h, hval idx);
hval hpbi_ui_clear(hval h);
hval hpbi_ui_ctrl_show(hval h, hval vis);
hval hpbi_ui_selected(hval h);
hval hpbi_ui_select(hval h, hval idx);
hval hpbi_ui_item_text(hval h, hval idx);
hval hpbi_ui_check_get(hval h);
hval hpbi_ui_check_set(hval h, hval on);
hval hpbi_ui_progress(hval h, hval pct);
hval hpbi_ui_slider_get(hval h);
hval hpbi_ui_slider_set(hval h, hval pos);
hval hpbi_ui_bg(hval h, hval rgb);
hval hpbi_ui_fg(hval h, hval rgb);
hval hpbi_ui_font(hval h, hval size, hval bold);
hval hpbi_ui_menu(hval win, hval label);
hval hpbi_ui_menu_item(hval menu, hval label, hval cmdid);
hval hpbi_ui_menu_sep(hval menu);
hval hpbi_ui_menu_check(hval h);
hval hpbi_ui_on(hval h, hval ev, hval cb);
hval hpbi_ui_timer(hval ms, hval cb);
hval hpbi_ui_msg(hval win, hval title, hval text, hval type);
hval hpbi_ui_open_file(hval win, hval filter);
hval hpbi_ui_save_file(hval win, hval filter);
hval hpbi_ui_pick_folder(hval win);
hval hpbi_ui_pick_color(hval win, hval init);
hval hpbi_ui_clip_set(hval text);
hval hpbi_ui_clip_get(void);

/* heap boxes for by-ref captures (use (&$x)) */
hval *hp_box_new(void);
void hp_box_free(hval *b);

#ifdef __cplusplus
}
#endif
#endif
