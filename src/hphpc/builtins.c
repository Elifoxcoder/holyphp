/* builtins.c — PHP-compatible standard library function table. */
#include "builtins.h"

Type *ty_int, *ty_i8, *ty_i16, *ty_i32, *ty_i64,
    *ty_float, *ty_f32, *ty_f64, *ty_bool, *ty_string, *ty_void, *ty_mixed;

static Builtin *tbl = NULL;
static size_t tbl_len = 0, tbl_cap = 0;

Type *type_array_of(const Type *elem) {
    Type *t = type_new(TY_ARRAY);
    t->elem = elem;
    return t;
}

static void add(const char *name, BuiltinShape shape, const Type *ret,
                bool unsafe, bool variadic) {
    if (tbl_len == tbl_cap) {
        tbl_cap = tbl_cap ? tbl_cap * 2 : 64;
        tbl = xrealloc(tbl, tbl_cap * sizeof(Builtin));
    }
    tbl[tbl_len++] = (Builtin){ intern(name), shape, ret, unsafe, variadic };
}

void builtins_init(void) {
    if (ty_int) return;
    ty_int = type_new(TY_INT);
    ty_i8 = type_new(TY_I8); ty_i16 = type_new(TY_I16);
    ty_i32 = type_new(TY_I32); ty_i64 = type_new(TY_I64);
    ty_float = type_new(TY_FLOAT); ty_f32 = type_new(TY_F32); ty_f64 = type_new(TY_F64);
    ty_bool = type_new(TY_BOOL);
    ty_string = type_new(TY_STRING);
    ty_void = type_new(TY_VOID);
    ty_mixed = type_new(TY_MIXED);

    /* PHP string functions */
    add("strlen",        B_LEN,     ty_int,    false, false);
    add("count",         B_LEN,     ty_int,    false, false);
    add("sizeof",        B_LEN,     ty_int,    false, false);
    add("strtoupper",    B_STR1,    ty_string, false, false);
    add("strtolower",    B_STR1,    ty_string, false, false);
    add("ucfirst",       B_STR1,    ty_string, false, false);
    add("lcfirst",       B_STR1,    ty_string, false, false);
    add("trim",          B_STR1,    ty_string, false, false);
    add("ltrim",         B_MISC,    ty_string, false, false);
    add("rtrim",         B_MISC,    ty_string, false, false);
    add("chop",          B_STR1,    ty_string, false, false);
    add("strrev",        B_STR1,    ty_string, false, false);
    add("str_repeat",    B_INT_STR, ty_string, false, false);
    add("str_pad",       B_MISC,    ty_string, false, false);
    add("str_replace",   B_MISC,    ty_string, false, false);
    add("substr",        B_MISC,    ty_string, false, false);
    add("strstr",        B_STR2,    ty_string, false, false);
    add("strchr",        B_STR2,    ty_string, false, false);
    add("strpos",        B_MISC,    ty_mixed,  false, false);   /* (hay, needle[, offset]) -> int|false, PHP-style */
    add("str_contains",  B_STR2,    ty_bool,   false, false);
    add("str_starts_with", B_STR2,  ty_bool,   false, false);
    add("str_ends_with", B_STR2,    ty_bool,   false, false);
    add("strcmp",        B_CMP,     ty_int,    false, false);
    add("strncmp",       B_MISC,    ty_int,    false, false);
    add("strncasecmp",   B_MISC,    ty_int,    false, false);
    add("strcasecmp",    B_CMP,     ty_int,    false, false);
    add("explode",       B_STR2,    type_array_of(ty_string), false, false);
    add("split",         B_STR2,    type_array_of(ty_string), false, false);
    add("implode",       B_MISC,    ty_string, false, false);
    add("join",          B_MISC,    ty_string, false, false);
    add("nl2br",         B_STR1,    ty_string, false, false);
    add("number_format", B_MISC,    ty_string, false, false);
    add("unset",         B_MISC,    ty_void,   false, false);
    add("json_encode",   B_MISC,    ty_string, false, false);
    add("json_decode",   B_STR_INT, ty_mixed,  false, false);
    /* 1 arg, or 2 with PHP's $raw_output flag (real SHA-1/MD5 digests) */
    add("md5",           B_MISC,    ty_string, false, false);
    add("sha1",          B_MISC,    ty_string, false, false);
    add("crc32",         B_STR1,    ty_int,    false, false);
    add("base64_encode", B_STR1,    ty_string, false, false);
    add("base64_decode", B_STR1,    ty_string, false, false);
    add("urlencode",     B_STR1,    ty_string, false, false);
    add("urldecode",     B_STR1,    ty_string, false, false);
    add("htmlentities",  B_STR1,    ty_string, false, false);
    add("htmlspecialchars", B_STR1,  ty_string, false, false);
    add("sprintf",       B_MISC,    ty_string, false, false);
    add("printf",        B_MISC,    ty_int,    false, false);
    add("vsprintf",      B_MISC,    ty_string, false, false);
    add("ord",           B_STR1,    ty_int,    false, false);
    add("chr",           B_INT_INT, ty_string, false, false);
    add("bin2hex",       B_STR1,    ty_string, false, false);
    add("hex2bin",       B_STR1,    ty_string, false, false);
    add("str_split",     B_MISC,    type_array_of(ty_string), false, false);
    add("ucwords",       B_STR1,    ty_string, false, false);
    add("wordwrap",      B_MISC,    ty_string, false, false);
    add("similar_text",  B_STR2,    ty_int,    false, false);
    add("levenshtein",   B_STR2,    ty_int,    false, false);

    /* array functions */
    add("array",         B_ARRAY,   ty_mixed,  false, true);
    add("array_keys",    B_KEYS,    ty_mixed,  false, false);
    add("array_values",  B_VALUES,  ty_mixed,  false, false);
    add("array_merge",   B_MISC,    ty_mixed,  false, true);
    add("array_slice",   B_MISC,    ty_mixed,  false, false);
    add("array_reverse", B_SORTISH, ty_mixed,  false, false);
    add("array_sum",     B_MISC,    ty_mixed,  false, false);
    add("array_product", B_MISC,    ty_mixed,  false, false);
    add("array_unique",  B_SORTISH, ty_mixed,  false, false);
    add("in_array",      B_MISC,    ty_bool,   false, false);
    add("array_key_exists", B_MISC, ty_bool,   false, false);
    add("isset",         B_MISC,    ty_bool,   false, true);
    add("range",         B_RANGE,   ty_mixed,  false, false);
    add("compact",       B_MISC,    ty_mixed,  false, false);
    add("array_map",     B_MISC,    ty_mixed,  false, true);
    add("array_filter",  B_MISC,    ty_mixed,  false, true);
    add("array_reduce",  B_MISC,    ty_mixed,  false, false);
    add("array_flip",    B_SORTISH, ty_mixed,  false, false);
    add("array_fill",    B_MISC,    ty_mixed,  false, false);
    add("array_combine", B_MISC,    ty_mixed,  false, false);
    add("array_diff",    B_MISC,    ty_mixed,  false, true);
    add("array_intersect", B_MISC,  ty_mixed,  false, true);
    add("array_push",    B_MISC,    ty_int,    false, true);
    add("array_pop",     B_MISC,    ty_mixed,  false, false);
    add("array_shift",   B_MISC,    ty_mixed,  false, false);
    add("array_unshift", B_MISC,    ty_int,    false, true);
    add("array_splice",  B_MISC,    ty_mixed,  false, false);
    add("shuffle",       B_MISC,    ty_bool,   false, false);
    add("sort",          B_SORT,    ty_bool,   false, false);
    add("rsort",         B_SORT,    ty_bool,   false, false);
    add("ksort",         B_SORT,    ty_bool,   false, false);
    add("krsort",        B_SORT,    ty_bool,   false, false);
    add("asort",         B_SORT,    ty_bool,   false, false);
    add("array_search",   B_MISC,    ty_mixed,  false, false);
    add("end",           B_MISC,    ty_mixed,  false, false);
    add("reset",         B_MISC,    ty_mixed,  false, false);

    /* math */
    add("max",           B_MAXMIN,  ty_mixed,  false, true);
    add("min",           B_MAXMIN,  ty_mixed,  false, true);
    add("rand",          B_MISC,    ty_int,    false, false);
    add("mt_rand",       B_MISC,    ty_float,  false, false);
    add("srand",         B_MISC,    ty_void,   false, false);
    add("mt_srand",      B_MISC,    ty_void,   false, false);
    add("random_int",    B_MISC,    ty_int,    false, false);
    add("fgetc",         B_MISC,    ty_string, false, false);
    add("abs",           B_ABS,     ty_mixed,  false, false);
    add("round",         B_MISC,    ty_mixed,  false, false);
    add("floor",         B_ABS,     ty_float,  false, false);
    add("ceil",          B_ABS,     ty_float,  false, false);
    add("sqrt",          B_ABS,     ty_float,  false, false);
    add("pow",           B_MISC,    ty_mixed,  false, false);
    add("intdiv",        B_MISC,    ty_int,    false, false);
    add("fmod",          B_MISC,    ty_float,  false, false);
    add("sin",           B_ABS,     ty_float,  false, false);
    add("cos",           B_ABS,     ty_float,  false, false);
    add("tan",           B_ABS,     ty_float,  false, false);
    add("atan",          B_ABS,     ty_float,  false, false);
    add("atan2",         B_ABS,     ty_float,  false, false);
    add("asin",          B_ABS,     ty_float,  false, false);
    add("acos",          B_ABS,     ty_float,  false, false);
    add("log",           B_ABS,     ty_float,  false, false);
    add("log2",          B_ABS,     ty_float,  false, false);
    add("log10",         B_ABS,     ty_float,  false, false);
    add("exp",           B_ABS,     ty_float,  false, false);
    add("is_nan",        B_ABS,     ty_bool,   false, false);
    add("is_finite",     B_ABS,     ty_bool,   false, false);
    add("is_infinite",   B_ABS,     ty_bool,   false, false);
    add("pi",            B_MISC,    ty_float,  false, false);
    add("M_PI",          B_MISC,    ty_float,  false, false);
    add("deg2rad",       B_ABS,     ty_float,  false, false);
    add("rad2deg",       B_ABS,     ty_float,  false, false);
    add("hypot",         B_MISC,    ty_float,  false, false);
    add("lcg_value",     B_MISC,    ty_float,  false, false);

    /* type juggling helpers */
    add("is_int",        B_ABS,     ty_bool,   false, false);
    add("is_integer",    B_ABS,     ty_bool,   false, false);
    add("is_float",      B_ABS,     ty_bool,   false, false);
    add("is_string",     B_ABS,     ty_bool,   false, false);
    add("is_bool",       B_ABS,     ty_bool,   false, false);
    add("is_array",      B_ABS,     ty_bool,   false, false);
    add("is_null",       B_ABS,     ty_bool,   false, false);
    add("is_numeric",    B_ABS,     ty_bool,   false, false);
    add("intval",        B_ABS,     ty_int,    false, false);
    add("floatval",      B_ABS,     ty_float,  false, false);
    add("doubleval",     B_ABS,     ty_float,  false, false);
    add("strval",        B_ABS,     ty_string, false, false);
    add("boolval",       B_ABS,     ty_bool,   false, false);
    add("gettype",       B_ABS,     ty_string, false, false);
    add("var_dump",      B_MISC,    ty_void,   false, true);
    add("print_r",       B_MISC,    ty_string, false, true);
    add("var_export",    B_MISC,    ty_string, false, true);
    add("serialize",     B_ABS,     ty_string, false, false);
    add("unserialize",   B_ABS,     ty_mixed,  false, false);

    /* io / process */
    add("readline",      B_STR1,    ty_string, false, false);
    add("file_get_contents", B_STR1, ty_string, false, false);
    add("file_put_contents", B_STR2, ty_int,    false, false);
/* filesystem helpers */
    add("file_exists",   B_STR1,    ty_bool,   false, false);
    add("is_dir",        B_STR1,    ty_bool,   false, false);
    add("filesize",      B_STR1,    ty_int,    false, false);
    add("mkdir",         B_STR1,    ty_bool,   false, false);
    add("rmdir",         B_STR1,    ty_bool,   false, false);
    add("unlink",        B_STR1,    ty_bool,   false, false);
    add("rename",        B_STR2,    ty_bool,   false, false);
    add("copy",          B_STR2,    ty_bool,   false, false);
    add("fgets",         B_STR1,    ty_string, false, false);
    add("fwrite",        B_STR2,    ty_int,    false, false);
    add("fopen",         B_STR2,    ty_mixed,  false, false);
    add("fclose",        B_STR1,    ty_bool,   false, false);
    add("scandir",       B_STR1,    type_array_of(ty_string), false, false);
    /* sockets — the transport layer for library code (lib/websocket.hphp) */
    add("stream_socket_server", B_MISC, ty_mixed,  false, false);
    add("stream_socket_client", B_MISC, ty_mixed,  false, false);
    add("stream_socket_accept", B_MISC, ty_mixed,  false, false);
    add("stream_socket_accept2", B_MISC, ty_mixed, false, false);   /* (srv, timeoutMs): false on timeout */
    add("stream_set_blocking",  B_MISC, ty_bool,   false, false);
    add("stream_ready",         B_MISC, type_array_of(ty_mixed), false, false);
    add("stream_recv",          B_MISC, ty_string, false, false);
    add("stream_send",          B_MISC, ty_int,    false, false);
    add("stream_eof",           B_MISC, ty_bool,   false, false);
    add("stream_peer",          B_MISC, ty_string, false, false);
    add("stream_close",         B_MISC, ty_bool,   false, false);
    add("getcwd",       B_ZERO,    ty_string, false, false);
    /* number base conversion */
    add("decbin",        B_INT_INT, ty_string, false, false);
    add("decoct",        B_INT_INT, ty_string, false, false);
    add("dechex",        B_INT_INT, ty_string, false, false);
    add("bindec",        B_STR1,    ty_int,    false, false);
    add("octdec",        B_STR1,    ty_int,    false, false);
    add("hexdec",        B_STR1,    ty_int,    false, false);
    add("getenv",        B_STR1,    ty_string, false, false);
    add("putenv",        B_STR1,    ty_bool,   false, false);
    add("php_uname",     B_MISC,    ty_string, false, false);
    add("phpversion",    B_MISC,    ty_string, false, false);
    add("php_sapi_name", B_MISC,    ty_string, false, false);
    add("microtime",     B_MISC,    ty_float,  false, false);
    add("hrtime",        B_MISC,    ty_int,    false, false);
    add("memory_get_usage", B_MISC, ty_int,    false, false);
    add("memory_get_peak_usage", B_MISC, ty_int, false, false);
    add("gc_collect_cycles", B_MISC, ty_int,   false, false);
    add("sys_getloadavg", B_MISC,   ty_mixed,  false, false);
    add("php_logo_guid", B_MISC,    ty_string, false, false);
    add("date",          B_STR1,    ty_string, false, false);
    add("time",          B_MISC,    ty_int,    false, false);
    add("usleep",        B_INT_INT, ty_void,   false, false);
    add("sleep",         B_INT_INT, ty_int,    false, false);
    add("exit",          B_MISC,    ty_void,   false, false);
    add("die",           B_MISC,    ty_void,   false, false);
    add("php_sapi",      B_MISC,    ty_string, false, false);

    /* UI layer (hphp_ui.c): windows, controls, menus, dialogs, timers */
    add("ui_dispatch",    B_INT_INT, ty_int,     false, false);
    add("ui_run_main",    B_ZERO,    ty_void,    false, false);
    add("ui_quit",        B_ZERO,    ty_void,    false, false);
    add("ui_window",      B_MISC,    ty_int,     false, false);
    add("ui_show",        B_MISC,    ty_int,     false, false);
    add("ui_title",       B_MISC,    ty_int,     false, false);
    add("ui_size",        B_MISC,    ty_int,     false, false);
    add("ui_pos",         B_MISC,    ty_int,     false, false);
    add("ui_close",       B_MISC,    ty_int,     false, false);
    add("ui_alive",       B_MISC,    ty_bool,    false, false);
    add("ui_add",         B_MISC,    ty_int,     false, false);
    add("ui_set",         B_MISC,    ty_int,     false, false);
    add("ui_get",         B_MISC,    ty_string,  false, false);
    add("ui_enable",      B_MISC,    ty_int,     false, false);
    add("ui_move",        B_MISC,    ty_int,     false, false);
    add("ui_focus",       B_MISC,    ty_int,     false, false);
    add("ui_items",       B_MISC,    ty_int,     false, false);
    add("ui_remove",      B_MISC,    ty_int,     false, false);
    add("ui_clear",       B_MISC,    ty_int,     false, false);
    add("ui_ctrl_show",   B_MISC,    ty_int,     false, false);
    add("ui_selected",    B_MISC,    ty_int,     false, false);
    add("ui_select",      B_MISC,    ty_int,     false, false);
    add("ui_item_text",   B_MISC,    ty_string,  false, false);
    add("ui_check_get",   B_MISC,    ty_int,     false, false);
    add("ui_check_set",   B_MISC,    ty_int,     false, false);
    add("ui_progress",    B_MISC,    ty_int,     false, false);
    add("ui_slider_get",  B_MISC,    ty_int,     false, false);
    add("ui_slider_set",  B_MISC,    ty_int,     false, false);
    add("ui_bg",          B_MISC,    ty_int,     false, false);
    add("ui_fg",          B_MISC,    ty_int,     false, false);
    add("ui_font",        B_MISC,    ty_int,     false, false);
    add("ui_menu",        B_MISC,    ty_int,     false, false);
    add("ui_menu_item",   B_MISC,    ty_int,     false, false);
    add("ui_menu_sep",    B_MISC,    ty_int,     false, false);
    add("ui_menu_check",  B_MISC,    ty_int,     false, false);
    add("ui_on",          B_MISC,    ty_int,     false, false);
    add("ui_timer",       B_MISC,    ty_int,     false, false);
    add("ui_msg",         B_MISC,    ty_int,     false, false);
    add("ui_open_file",   B_MISC,    ty_mixed,   false, false);
    add("ui_save_file",   B_MISC,    ty_mixed,   false, false);
    add("ui_pick_folder", B_MISC,    ty_mixed,   false, false);
    add("ui_pick_color",  B_MISC,    ty_mixed,   false, false);
    add("ui_clip_set",    B_STR1,    ty_int,     false, false);
    add("ui_clip_get",    B_ZERO,    ty_string,  false, false);

    /* unsafe: raw memory & ffi */
    add("malloc",        B_INT_INT, ty_mixed,  true,  false);
    add("free",          B_MISC,    ty_void,   true,  false);
    add("memcpy",        B_MISC,    ty_mixed,  true,  false);
    add("memset",        B_MISC,    ty_mixed,  true,  false);
    add("ffi_load",      B_STR1,    ty_mixed,  true,  false);
    add("ffi_call",      B_MISC,    ty_mixed,  true,  true);
    add("heap_dump",     B_MISC,    ty_string, true,  false);
}

const Builtin *builtin_lookup(const char *name) {
    for (size_t i = 0; i < tbl_len; i++)
        if (tbl[i].name == intern(name)) return &tbl[i];
    return NULL;
}
