/* runtime_fuzz.c — libFuzzer target for the HolyPHP runtime builtins.
 *
 * Calls the string, array, JSON, encoding and hashing builtins with values
 * built from the fuzz input. Exceptions (empty separator, bad format, …) are
 * part of normal behaviour: a try frame catches them. The fuzzer looks for
 * memory errors and crashes inside the runtime.
 *
 * Build and run: bash tests/fuzz/run.sh runtime
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hphp_ext.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    hp_init();
    return 0;
}

static hval mk(const uint8_t *d, size_t n) {
    return hp_of_str(hp_str_new((const char *)d, n));
}

static hval mk_i(int64_t i) { return hp_of_int(i); }

static void run(uint8_t op, const uint8_t *d, size_t n) {
    hval s = mk(d, n);
    hval s2 = mk(d, n / 2);
    hval key = mk(d + (n / 3), n / 4);
    hval glue = mk(d + (n / 2), n / 8);
    hval one = mk_i((int64_t)(d[0] % 64));
    hval two = mk_i((int64_t)(d[1] % 32));
    hval flag = hp_of_bool((d[2] & 1) != 0);

    harr *a = hp_arr_new();
    hp_arr_push(a, s);
    hp_arr_push(a, s2);
    if (n > 8) hp_arr_push(a, key);
    hp_arr_push(a, one);
    hval arr = hp_of_arr(a);

    switch (op) {
    case 0:  (void)hpbi_base64_encode(s); break;
    case 1:  (void)hpbi_base64_decode(s); break;
    case 2:  (void)hpbi_bin2hex(s); break;
    case 3:  (void)hpbi_hex2bin(s); break;
    case 4:  (void)hpbi_urlencode(s); break;
    case 5:  (void)hpbi_urldecode(s); break;
    case 6:  (void)hpbi_json_encode(s); break;
    case 7:  (void)hpbi_json_encode(arr); break;
    case 8:  (void)hpbi_json_decode(s); break;
    case 9:  (void)hpbi_json_decode2(s, flag); break;
    case 10: (void)hpbi_explode(key, s); break;
    case 11: (void)hpbi_implode(glue, arr); break;
    case 12: (void)hpbi_substr(s, one, two); break;
    case 13: (void)hpbi_str_replace(s, key, glue); break;
    case 14: (void)hpbi_str_repeat(s, mk_i((int64_t)(d[0] % 64))); break;
    case 15: (void)hpbi_str_pad(s, mk_i((int64_t)(d[1] % 96)), key); break;
    case 16: (void)hpbi_str_split(s, mk_i((int64_t)(1 + d[0] % 8))); break;
    case 17: (void)hpbi_trim(s); break;
    case 18: (void)hpbi_strtolower(s); break;
    case 19: (void)hpbi_strtoupper(s); break;
    case 20: (void)hpbi_ucfirst(s); break;
    case 21: (void)hpbi_nl2br(s); break;
    case 22: (void)hpbi_htmlspecialchars(s); break;
    case 23: (void)hpbi_strip_tags(s, key); break;
    case 24: (void)hpbi_sha1(s, flag); break;
    case 25: (void)hpbi_md5(s, flag); break;
    case 26: (void)hpbi_sha256(s); break;
    case 27: (void)hpbi_hash_hmac(hp_of_str(hp_str_lit("sha256")), s, key, flag); break;
    case 28: (void)hpbi_crc32(s); break;
    case 29: (void)hpbi_array_merge(arr, arr, arr, arr); break;
    case 30: (void)hpbi_array_slice(arr, one, two); break;
    case 31: (void)hpbi_array_reverse(arr); break;
    case 32: (void)hpbi_array_unique(arr); break;
    case 33: (void)hpbi_array_sum(arr); break;
    case 34: (void)hpbi_strpos(s, key, one); break;
    case 36: (void)hpbi_substr_count(s, key); break;
    case 37: (void)hpbi_number_format(s, mk_i((int64_t)(d[0] % 8)), glue, key); break;
    default: break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) return 0;
    if (size > 128 * 1024) return 0;

    /* work on a padded copy: the slices below deliberately overlap and may
     * run to the end of the input */
    size_t n = size - 4;
    unsigned char *buf = (unsigned char *)malloc(n + 8);
    if (!buf) return 0;
    memcpy(buf, data + 4, n);
    memset(buf + n, 0, 8);

    /* A builtin may throw (empty separator, bad number, …). That is expected
     * behaviour, not a finding, so catch it and continue. */
    hp_try_frame fr;
    hp_try_push(&fr);
    if (setjmp(fr.jmp) == 0) {
        run((uint8_t)(data[0] % 38), buf, n);
    }
    hp_try_pop(&fr);
    free(buf);
    return 0;
}
