/* hphp_std.c — PHP-compatible standard library builtins.
 *
 * Each function matches the hpbi_<name> prototype contract: hval args,
 * hval return. Generated code projects scalars out of results.
 */
#include "hphp_rt.h"

#ifndef HPHP_VERSION
#define HPHP_VERSION "1.0.0"
#endif

static void hbuf_json_r(hval v, Buf *b);

#include <ctype.h>

/* shares the core's PHP is_numeric() rule, so is_numeric()/is_int() and the
 * arithmetic operators agree on what counts as a numeric string */
static bool hp_num_like(hval v) {
    return v.tag == HV_INT || v.tag == HV_FLOAT || v.tag == HV_BOOL ||
           (v.tag == HV_STR && hp_str_is_numeric(v.u.s));
}

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#if defined(_WIN32)
/* winsock2.h must precede windows.h, otherwise the old winsock1 clashing
 * declarations from windows.h win and the socket calls do not link. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#define mkdir(p) _mkdir(p)
typedef SOCKET hp_sock;
#define HP_SOCK_BAD INVALID_SOCKET
#define hp_sock_close(s) closesocket(s)
#define hp_sock_wouldblock() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
typedef int hp_sock;
#define HP_SOCK_BAD (-1)
#define hp_sock_close(s) close(s)
#define hp_sock_wouldblock() (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
#endif

static void itoa_bin(int64_t v, int base, char *out) {
    static const char digits[] = "0123456789abcdef";
    char buf[80];
    int i = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
    if (u == 0) buf[i++] = '0';
    while (u) { buf[i++] = digits[u % (unsigned)base]; u /= (unsigned)base; }
    char *o = out;
    if (neg) *o++ = '-';
    while (i) *o++ = buf[--i];
    *o = 0;
}
#include <time.h>
#include <ctype.h>

#define RETI(v) return hp_of_int(v)
#define RETF(v) return hp_of_float(v)
#define RETB(v) return hp_of_bool(v)
#define RETS(v) return hp_of_str(v)

/* ---------- strings ---------- */
hval hpbi_strlen(hval s) { RETI(hp_str_len(hp_val_to_str(s))); }
hval hpbi_count(hval a) {
    if (a.tag == HV_STR) RETI(hp_str_len(a.u.s));
    if (a.tag == HV_ARR) RETI(hp_arr_len(a.u.a));
    RETI(0);
}
hval hpbi_sizeof(hval a) { return hpbi_count(a); }
hval hpbi_strtoupper(hval s) { RETS(hp_str_toupper(hp_val_to_str(s))); }
hval hpbi_strtolower(hval s) { RETS(hp_str_tolower(hp_val_to_str(s))); }
hval hpbi_ucfirst(hval s) {
    hstr *x = hp_val_to_str(s);
    if (!x->len) RETS(x);
    hstr *r = hp_str_copy(x);
    r->data[0] = (char)toupper((unsigned char)r->data[0]);
    RETS(r);
}
hval hpbi_lcfirst(hval s) {
    hstr *x = hp_val_to_str(s);
    if (!x->len) RETS(x);
    hstr *r = hp_str_copy(x);
    r->data[0] = (char)tolower((unsigned char)r->data[0]);
    RETS(r);
}
hval hpbi_trim(hval s) { RETS(hp_str_trim(hp_val_to_str(s))); }
hval hpbi_ltrim(hval s, hval chars) {
    hstr *x = hp_val_to_str(s);
    const char *set = chars.tag == HV_STR ? chars.u.s->data : " ";
    size_t slen = chars.tag == HV_STR ? chars.u.s->len : 1;
    size_t a = 0;
    while (a < x->len && (isspace((unsigned char)x->data[a]) || memchr(set, x->data[a], slen))) a++;
    RETS(hp_str_new(x->data + a, x->len - a));
}
hval hpbi_rtrim(hval s, hval chars) {
    hstr *x = hp_val_to_str(s);
    const char *set = chars.tag == HV_STR ? chars.u.s->data : " ";
    size_t slen = chars.tag == HV_STR ? chars.u.s->len : 1;
    size_t b = x->len;
    while (b > 0 && (isspace((unsigned char)x->data[b - 1]) || memchr(set, x->data[b - 1], slen))) b--;
    RETS(hp_str_new(x->data, b));
}
hval hpbi_chop(hval s) { RETS(hp_str_new(hp_val_to_str(s)->data, 0)); } /* chop = rtrim($s) in PHP */
hval hpbi_strrev(hval s) { RETS(hp_str_reverse(hp_val_to_str(s))); }
hval hpbi_str_repeat(hval s, hval n) {
    RETS(hp_str_repeat(hp_val_to_str(s), hp_val_to_int(n)));
}
hval hpbi_str_pad(hval s, hval n, hval pad) {
    hstr *x = hp_val_to_str(s);
    int64_t want = hp_val_to_int(n);
    if ((int64_t)x->len >= want) RETS(x);
    hstr *p = hp_val_to_str(pad);
    if (!p->len) p = hp_str_lit(" ");
    hstr *fill = hp_str_repeat(p, (want - (int64_t)x->len + p->len - 1) / p->len);
    RETS(hp_str_concat2(x, hp_str_slice(fill, 0, want - (int64_t)x->len)));
}
hval hpbi_str_replace(hval subject, hval search, hval replace) {
    RETS(hp_str_replace(hp_val_to_str(subject), hp_val_to_str(search), hp_val_to_str(replace)));
}
hval hpbi_substr(hval s, hval start, hval len) {
    hstr *x = hp_val_to_str(s);
    int64_t st = hp_val_to_int(start);
    st = st < 0 ? st + (int64_t)x->len : st;
    if (st < 0) st = 0;
    if ((size_t)st > x->len) RETS(hp_str_lit(""));
    int64_t l = len.tag == HV_NULL ? (int64_t)x->len - st : hp_val_to_int(len);
    if (l < 0) l = (int64_t)x->len - st + l;
    if (l < 0) l = 0;
    RETS(hp_str_slice(x, st, st + l));
}
hval hpbi_strstr(hval h, hval n) {
    int64_t i = hp_str_index(hp_val_to_str(h), hp_val_to_str(n));
    if (i < 0) RETB(false);
    RETS(hp_str_slice(hp_val_to_str(h), i, hp_str_len(hp_val_to_str(h))));
}
hval hpbi_strchr(hval h, hval n) { return hpbi_strstr(h, n); }
hval hpbi_strpos(hval h, hval n, hval off) {
    hstr *hay = hp_val_to_str(h);
    long long o = (hp_tag(off) == HV_NULL) ? 0 : off.u.i;
    if (o < 0) o = 0;
    if ((size_t)o > hay->len) o = (long long)hay->len;
    hstr *tail = hp_str_new(hay->data + o, hay->len - (size_t)o);
    int64_t r = hp_str_index(tail, hp_val_to_str(n));
    if (r < 0) return hp_of_bool(false);   /* PHP: strpos miss => false */
    RETI(r + o);                            /* absolute position */
}
hval hpbi_str_contains(hval h, hval n) {
    RETB(hp_str_index(hp_val_to_str(h), hp_val_to_str(n)) >= 0);
}
hval hpbi_str_starts_with(hval h, hval n) {
    hstr *x = hp_val_to_str(h), *y = hp_val_to_str(n);
    RETB(x->len >= y->len && memcmp(x->data, y->data, y->len) == 0);
}
hval hpbi_str_ends_with(hval h, hval n) {
    hstr *x = hp_val_to_str(h), *y = hp_val_to_str(n);
    RETB(x->len >= y->len && memcmp(x->data + x->len - y->len, y->data, y->len) == 0);
}
hval hpbi_strcmp(hval a, hval b) { RETI(hp_str_cmp(hp_val_to_str(a), hp_val_to_str(b))); }
hval hpbi_strncmp(hval a, hval b, hval n) {
    hstr *x = hp_val_to_str(a), *y = hp_val_to_str(b);
    int64_t k = hp_val_to_int(n);
    if (k < 0) k = 0;
    if ((size_t)k > x->len) k = (int64_t)x->len;
    if ((size_t)k > y->len) k = (int64_t)y->len;
    int r = k ? memcmp(x->data, y->data, (size_t)k) : 0;
    RETI(r < 0 ? -1 : r > 0 ? 1 : 0);
}
static int hp_str_casecmp_n(hstr *a, hstr *b) {
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a->data[i]);
        int cb = tolower((unsigned char)b->data[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}
hval hpbi_strcasecmp(hval a, hval b) {
    RETI(hp_str_casecmp_n(hp_val_to_str(a), hp_val_to_str(b)));
}
hval hpbi_strncasecmp(hval a, hval b, hval n) {
    hstr *x = hp_val_to_str(a), *y = hp_val_to_str(b);
    size_t k = (size_t)hp_val_to_int(n);
    if (k > x->len) k = x->len;
    if (k > y->len) k = y->len;
    hstr *xs = hp_str_new(x->data, k), *ys = hp_str_new(y->data, k);
    RETI(hp_str_casecmp_n(xs, ys));
}
hval hpbi_explode(hval sep, hval s) {
    hstr *x = hp_val_to_str(s), *d = hp_val_to_str(sep);
    harr *r = hp_arr_new();
    if (d->len == 0) { hp_throw_str("explode(): Empty delimiter"); RETS(hp_str_lit("")); }
    size_t start = 0;
    for (size_t i = 0; i + d->len <= x->len;) {
        if (memcmp(x->data + i, d->data, d->len) == 0) {
            hp_arr_push(r, hp_of_str(hp_str_new(x->data + start, i - start)));
            i += d->len;
            start = i;
        } else i++;
    }
    hp_arr_push(r, hp_of_str(hp_str_new(x->data + start, x->len - start)));
    return hp_of_arr(r);
}
hval hpbi_split(hval sep, hval s) { return hpbi_explode(sep, s); }
static hval hp_implode(hval glue, harr *a) {
    hstr *g = hp_val_to_str(glue);
    hstr *out = hp_str_lit("");
    for (size_t i = 0; i < a->len; i++) {
        if (i) out = hp_str_concat2(out, g);
        out = hp_str_concat2(out, hp_val_to_str(a->vals[i]));
    }
    RETS(out);
}
hval hpbi_implode(hval glue, hval arr) {
    if (arr.tag == HV_ARR) return hp_implode(glue, arr.u.a);
    return hp_implode(arr, glue.tag == HV_ARR ? glue.u.a : hp_null_arr());
}
hval hpbi_join(hval glue, hval arr) { return hpbi_implode(glue, arr); }
hval hpbi_nl2br(hval s) {
    RETS(hp_str_replace(hp_val_to_str(s), hp_str_lit("\n"), hp_str_lit("<br />\n")));
}
/* PHP unset($arr[$k]): remove by key from an array held in a variable.
 * The codegen passes the variable's harr* directly. */
hval hpbi_unset(harr *a, hval key) {
    if (a) hp_arr_unset(a, key);
    return hp_null;
}

hval hpbi_number_format(hval n, hval dec, hval dsep, hval tsep) {
    int64_t d = hp_val_to_int(dec);
    if (d < 0) d = 0;
    if (d > 18) d = 18;
    const char *ds = dsep.tag == HV_STR && dsep.u.s->len ? dsep.u.s->data : ".";
    const char *ts = tsep.tag == HV_STR ? tsep.u.s->data : ",";
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", (int)d, hp_val_to_float(n));
    /* insert thousands separators into the integer part */
    char out[96];
    size_t oi = 0, bi = 0;
    if (buf[0] == '-') out[oi++] = buf[bi++];
    const char *dot = strchr(buf + bi, '.');
    size_t intlen = dot ? (size_t)(dot - (buf + bi)) : strlen(buf + bi);
    for (size_t k = 0; k < intlen; k++) {
        if (k && (intlen - k) % 3 == 0 && ts[0]) out[oi++] = ts[0];
        out[oi++] = buf[bi + k];
    }
    if (dot) snprintf(out + oi, sizeof out - oi, "%s%s", ds, dot + 1);
    else out[oi] = 0;
    RETS(hp_str_lit(out));
}
hval hpbi_json_encode(hval v) {
    /* minimal JSON serializer */
    Buf b;
    buf_init(&b);
    hbuf_json_r(v, &b);
    char *s = hbuf_take(&b);
    hstr *r = hp_str_lit(s);
    free(s);
    return hp_of_str(r);
}
static void hbuf_json_r(hval v, Buf *b) {
    switch (v.tag) {
    case HV_NULL: hbuf_puts(b, "null"); break;
    case HV_BOOL: hbuf_puts(b, v.u.b ? "true" : "false"); break;
    case HV_INT: hbuf_printf(b, "%lld", (long long)v.u.i); break;
    case HV_FLOAT: hbuf_printf(b, "%g", v.u.f); break;
    case HV_STR:
        hbuf_puts(b, "\"");
        for (size_t i = 0; i < v.u.s->len; i++) {
            char c = v.u.s->data[i];
            if (c == '"' || c == '\\') hbuf_printf(b, "\\%c", c);
            else if (c == '\n') hbuf_puts(b, "\\n");
            else if (c == '\t') hbuf_puts(b, "\\t");
            else hbuf_putc(b, c);
        }
        hbuf_puts(b, "\"");
        break;
    case HV_ARR: {
        harr *a = v.u.a;
        if (a->is_map) {
            hbuf_puts(b, "{");
            for (size_t i = 0; i < a->len; i++) {
                if (i) hbuf_puts(b, ",");
                hval k = a->keys[i];
                hstr *ks = hp_val_to_str(k);
                hbuf_printf(b, "\"%.*s\":", (int)ks->len, ks->data);
                hbuf_json_r(a->vals[i], b);
            }
            hbuf_puts(b, "}");
        } else {
            hbuf_puts(b, "[");
            for (size_t i = 0; i < a->len; i++) {
                if (i) hbuf_puts(b, ",");
                hbuf_json_r(a->vals[i], b);
            }
            hbuf_puts(b, "]");
        }
        break;
    }
    default: hbuf_puts(b, "null"); break;
    }
}
/* ---- json_decode: a small recursive-descent JSON parser ---- */
typedef struct {
    const char *p;
    size_t len, pos;
} JsonP;

static void jp_ws(JsonP *j) {
    while (j->pos < j->len) {
        char c = j->p[j->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->pos++;
        else break;
    }
}

static hval jp_value(JsonP *j);

static hstr *jp_string(JsonP *j) {
    if (j->pos >= j->len || j->p[j->pos] != '"') return hp_str_lit("");
    j->pos++;
    Buf b;
    buf_init(&b);
    while (j->pos < j->len && j->p[j->pos] != '"') {
        char c = j->p[j->pos++];
        if (c == '\\' && j->pos < j->len) {
            char e = j->p[j->pos++];
            switch (e) {
            case 'n': buf_putc(&b, '\n'); break;
            case 't': buf_putc(&b, '\t'); break;
            case 'r': buf_putc(&b, '\r'); break;
            case 'b': buf_putc(&b, '\b'); break;
            case 'f': buf_putc(&b, '\f'); break;
            case 'u': {
                /* \uXXXX — emit UTF-8 (BMP only) */
                if (j->pos + 4 <= j->len) {
                    unsigned cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = j->p[j->pos++];
                        cp = cp * 16 + (h >= '0' && h <= '9' ? (unsigned)(h - '0')
                             : h >= 'a' && h <= 'f' ? (unsigned)(h - 'a' + 10)
                             : (unsigned)(h - 'A' + 10));
                    }
                    if (cp < 0x80) buf_putc(&b, (char)cp);
                    else if (cp < 0x800) {
                        buf_putc(&b, (char)(0xC0 | (cp >> 6)));
                        buf_putc(&b, (char)(0x80 | (cp & 63)));
                    } else {
                        buf_putc(&b, (char)(0xE0 | (cp >> 12)));
                        buf_putc(&b, (char)(0x80 | ((cp >> 6) & 63)));
                        buf_putc(&b, (char)(0x80 | (cp & 63)));
                    }
                }
                break;
            }
            default: buf_putc(&b, e); break;
            }
        } else buf_putc(&b, c);
    }
    if (j->pos < j->len) j->pos++;  /* closing quote */
    return hp_str_new(buf_take(&b), b.len);
}

static hval jp_value(JsonP *j) {
    jp_ws(j);
    if (j->pos >= j->len) return hp_null;
    char c = j->p[j->pos];
    if (c == '{') {
        j->pos++;
        harr *m = hp_arr_new();
        m->is_map = true;
        jp_ws(j);
        if (j->pos < j->len && j->p[j->pos] == '}') { j->pos++; return hp_of_arr(m); }
        for (;;) {
            jp_ws(j);
            hstr *k = jp_string(j);
            jp_ws(j);
            if (j->pos < j->len && j->p[j->pos] == ':') j->pos++;
            hval v = jp_value(j);
            hp_arr_set(m, hp_of_str(k), v);
            jp_ws(j);
            if (j->pos < j->len && j->p[j->pos] == ',') { j->pos++; continue; }
            if (j->pos < j->len && j->p[j->pos] == '}') { j->pos++; }
            break;
        }
        return hp_of_arr(m);
    }
    if (c == '[') {
        j->pos++;
        harr *a = hp_arr_new();
        jp_ws(j);
        if (j->pos < j->len && j->p[j->pos] == ']') { j->pos++; return hp_of_arr(a); }
        for (;;) {
            hval v = jp_value(j);
            hp_arr_push(a, v);
            jp_ws(j);
            if (j->pos < j->len && j->p[j->pos] == ',') { j->pos++; continue; }
            if (j->pos < j->len && j->p[j->pos] == ']') { j->pos++; }
            break;
        }
        return hp_of_arr(a);
    }
    if (c == '"') return hp_of_str(jp_string(j));
    if (j->pos + 4 <= j->len && memcmp(j->p + j->pos, "true", 4) == 0) { j->pos += 4; return hp_of_bool(true); }
    if (j->pos + 5 <= j->len && memcmp(j->p + j->pos, "false", 5) == 0) { j->pos += 5; return hp_of_bool(false); }
    if (j->pos + 4 <= j->len && memcmp(j->p + j->pos, "null", 4) == 0) { j->pos += 4; return hp_null; }
    /* number: int if no ./eE */
    {
        size_t st = j->pos;
        bool isf = false;
        while (j->pos < j->len) {
            char d = j->p[j->pos];
            if (d == '.' || d == 'e' || d == 'E') isf = true;
            else if (!((d >= '0' && d <= '9') || d == '-' || d == '+')) break;
            j->pos++;
        }
        if (j->pos == st) { j->pos++; return hp_null; }  /* garbage: skip */
        char tmp[64];
        size_t n = j->pos - st < 63 ? j->pos - st : 63;
        memcpy(tmp, j->p + st, n);
        tmp[n] = 0;
        if (isf) return hp_of_float(strtod(tmp, NULL));
        return hp_of_int((int64_t)strtoll(tmp, NULL, 10));
    }
}

hval hpbi_json_decode(hval s) {
    hstr *x = hp_val_to_str(s);
    JsonP j = { x->data, x->len, 0 };
    return jp_value(&j);
}
/* ---------------- message digests ----------------
 * Real SHA-1/MD5 (the WebSocket handshake in lib/websocket.hphp needs a
 * correct SHA-1: browsers verify Sec-WebSocket-Accept). Both accept PHP's
 * optional second argument: true = raw 20/16 bytes instead of hex. */

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void hp_sha1_digest(const unsigned char *msg, size_t len, unsigned char out[20]) {
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    size_t padded = ((len + 8) / 64 + 1) * 64;    /* always one pad byte + 8 */
    unsigned char *m = (unsigned char *)calloc(padded, 1);
    memcpy(m, msg, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) m[padded - 8 + i] = (unsigned char)(bits >> (56 - 8 * i));
    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)m[off + i * 4] << 24) | ((uint32_t)m[off + i * 4 + 1] << 16) |
                   ((uint32_t)m[off + i * 4 + 2] << 8) | (uint32_t)m[off + i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(m);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)h[i];
    }
}

static void hp_md5_digest(const unsigned char *msg, size_t len, unsigned char out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u };
    static const int S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    uint32_t h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    size_t padded = ((len + 8) / 64 + 1) * 64;
    unsigned char *m = (unsigned char *)calloc(padded, 1);
    memcpy(m, msg, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) m[padded - 8 + i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)m[off + i * 4] | ((uint32_t)m[off + i * 4 + 1] << 8) |
                   ((uint32_t)m[off + i * 4 + 2] << 16) | ((uint32_t)m[off + i * 4 + 3] << 24);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16)      { f = (b & c) | (~b & d);         g = i; }
            else if (i < 32) { f = (d & b) | (~d & c);         g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d;                  g = (3 * i + 5) % 16; }
            else             { f = c ^ (b | ~d);               g = (7 * i) % 16; }
            uint32_t tmp = d;
            d = c; c = b;
            b = b + rotl32(a + f + K[i] + w[g], S[i]);
            a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    free(m);
    for (int i = 0; i < 4; i++) {
        out[i * 4]     = (unsigned char)h[i];
        out[i * 4 + 1] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 3] = (unsigned char)(h[i] >> 24);
    }
}

static hstr *hp_hex(const unsigned char *d, size_t n) {
    static const char *hexd = "0123456789abcdef";
    char *s = (char *)malloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) {
        s[i * 2] = hexd[d[i] >> 4];
        s[i * 2 + 1] = hexd[d[i] & 15];
    }
    s[n * 2] = 0;
    hstr *out = hp_str_new(s, n * 2);
    free(s);
    return out;
}

hval hpbi_sha1(hval s, hval raw) {
    hstr *x = hp_val_to_str(s);
    unsigned char d[20];
    hp_sha1_digest((const unsigned char *)x->data, x->len, d);
    if (hp_val_to_bool(raw)) return hp_of_str(hp_str_new((const char *)d, 20));
    return hp_of_str(hp_hex(d, 20));
}

hval hpbi_md5(hval s, hval raw) {
    hstr *x = hp_val_to_str(s);
    unsigned char d[16];
    hp_md5_digest((const unsigned char *)x->data, x->len, d);
    if (hp_val_to_bool(raw)) return hp_of_str(hp_str_new((const char *)d, 16));
    return hp_of_str(hp_hex(d, 16));
}
hval hpbi_crc32(hval s) {
    hstr *x = hp_val_to_str(s);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < x->len; i++) {
        crc ^= (unsigned char)x->data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    RETI(~(int64_t)crc & 0xFFFFFFFFll);
}
static hstr *hp_b64_encode(hstr *in) {
    static const char *tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Buf b;
    buf_init(&b);
    size_t i = 0;
    while (i + 2 < in->len) {
        uint32_t n = ((unsigned char)in->data[i] << 16) | ((unsigned char)in->data[i + 1] << 8) | (unsigned char)in->data[i + 2];
        hbuf_putc(&b, tab[(n >> 18) & 63]); hbuf_putc(&b, tab[(n >> 12) & 63]);
        hbuf_putc(&b, tab[(n >> 6) & 63]); hbuf_putc(&b, tab[n & 63]);
        i += 3;
    }
    size_t rem = in->len - i;
    if (rem == 1) {
        uint32_t n = (unsigned char)in->data[i] << 16;
        hbuf_putc(&b, tab[(n >> 18) & 63]); hbuf_putc(&b, tab[(n >> 12) & 63]);
        hbuf_puts(&b, "==");
    } else if (rem == 2) {
        uint32_t n = ((unsigned char)in->data[i] << 16) | ((unsigned char)in->data[i + 1] << 8);
        hbuf_putc(&b, tab[(n >> 18) & 63]); hbuf_putc(&b, tab[(n >> 12) & 63]);
        hbuf_putc(&b, tab[(n >> 6) & 63]); hbuf_putc(&b, '=');
    }
    char *s = hbuf_take(&b);
    hstr *r = hp_str_lit(s);
    free(s);
    return r;
}
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static hstr *hp_b64_decode(hstr *in) {
    Buf b;
    buf_init(&b);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in->len; i++) {
        int v = b64_val(in->data[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            hbuf_putc(&b, (char)((acc >> bits) & 0xff));
        }
    }
    char *s = hbuf_take(&b);
    hstr *r = hp_str_new(s, b.len);
    free(s);
    return r;
}
hval hpbi_base64_encode(hval s) { RETS(hp_b64_encode(hp_val_to_str(s))); }
hval hpbi_base64_decode(hval s) { RETS(hp_b64_decode(hp_val_to_str(s))); }
hval hpbi_urlencode(hval s) {
    hstr *x = hp_val_to_str(s);
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < x->len; i++) {
        unsigned char c = (unsigned char)x->data[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') buf_putc(&b, (char)c);
        else if (c == ' ') buf_putc(&b, '+');
        else buf_printf(&b, "%%%02X", c);
    }
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_urldecode(hval s) {
    hstr *x = hp_val_to_str(s);
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < x->len; i++) {
        if (x->data[i] == '+') buf_putc(&b, ' ');
        else if (x->data[i] == '%' && i + 2 < x->len) {
            char hex[3] = { x->data[i + 1], x->data[i + 2], 0 };
            buf_putc(&b, (char)strtol(hex, NULL, 16));
            i += 2;
        } else buf_putc(&b, x->data[i]);
    }
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_htmlentities(hval s) {
    hstr *x = hp_val_to_str(s);
    hstr *r = hp_str_replace(x, hp_str_lit("&"), hp_str_lit("&amp;"));
    r = hp_str_replace(r, hp_str_lit("<"), hp_str_lit("&lt;"));
    r = hp_str_replace(r, hp_str_lit(">"), hp_str_lit("&gt;"));
    r = hp_str_replace(r, hp_str_lit("\""), hp_str_lit("&quot;"));
    RETS(r);
}
hval hpbi_htmlspecialchars(hval s) { return hpbi_htmlentities(s); }
hval hpbi_sprintf(hval fmt, hval args) {
    hstr *f = hp_val_to_str(fmt);
    harr *a = args.tag == HV_ARR ? args.u.a : hp_arr_of(1, &args);
    size_t ai = 0;
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < f->len; i++) {
        if (f->data[i] != '%') { buf_putc(&b, f->data[i]); continue; }
        size_t j = i + 1;
        if (j >= f->len) { buf_putc(&b, '%'); break; }
        if (f->data[j] == '%') { buf_putc(&b, '%'); i = j; continue; }
        /* parse %[flags][width][.precision]conv */
        char spec[32];
        size_t sp = 0;
        spec[sp++] = '%';
        while (j < f->len && sp < 24 && strchr("-+ 0#", f->data[j])) spec[sp++] = f->data[j++];
        while (j < f->len && sp < 24 && f->data[j] >= '0' && f->data[j] <= '9') spec[sp++] = f->data[j++];
        if (j < f->len && f->data[j] == '.') {
            spec[sp++] = '.';
            j++;
            while (j < f->len && sp < 24 && f->data[j] >= '0' && f->data[j] <= '9') spec[sp++] = f->data[j++];
        }
        char conv = j < f->len ? f->data[j] : 0;
        if (!conv) { buf_putc(&b, '%'); continue; }
        i = j;
        spec[sp] = 0;
        hval v = ai < a->len ? a->vals[ai++] : hp_null;
        switch (conv) {
        case 'd': case 'i': case 'u': {
            char cs[8]; snprintf(cs, sizeof cs, "ll%c", conv == 'i' ? 'd' : conv);
            char out[64]; snprintf(out, sizeof out, "%s%s", spec, cs);
            buf_printf(&b, out, (long long)hp_val_to_int(v));
            break;
        }
        case 'x': case 'X': case 'o': {
            char out[64]; snprintf(out, sizeof out, "%sll%c", spec, conv);
            buf_printf(&b, out, (unsigned long long)hp_val_to_int(v));
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            char out[64]; snprintf(out, sizeof out, "%s%c", spec, conv);
            buf_printf(&b, out, hp_val_to_float(v));
            break;
        }
        case 's': {
            hstr *s = hp_val_to_str(v);
            if (sp > 0 && spec[sp - 1] == '.') {
                char out[64]; snprintf(out, sizeof out, "%s.*s", spec);
                long prec = 0;
                sscanf(spec + 1, "%*[^.]%*c%ld", &prec);
                long n = prec < 0 || prec > (long)s->len ? (long)s->len : prec;
                buf_write(&b, s->data, (size_t)n);
            } else {
                buf_write(&b, s->data, s->len);
            }
            break;
        }
        case 'c': buf_putc(&b, (char)hp_val_to_int(v)); break;
        case 'b': {
            int64_t n = hp_val_to_int(v);
            char bits[65];
            bits[64] = 0;
            for (int k = 63; k >= 0; k--) bits[63 - k] = (n >> k) & 1 ? '1' : '0';
            buf_puts(&b, bits);
            break;
        }
        default:
            buf_puts(&b, spec);
            buf_putc(&b, conv);
            break;
        }
    }
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_printf(hval fmt, hval args) {
    hval r = hpbi_sprintf(fmt, args);
    hstr *s = r.u.s;
    fwrite(s->data, 1, s->len, stdout);
    RETI((int64_t)s->len);
}
hval hpbi_ord(hval s) {
    hstr *x = hp_val_to_str(s);
    RETI(x->len ? (unsigned char)x->data[0] : 0);
}
hval hpbi_chr(hval n) {
    char c = (char)hp_val_to_int(n);
    RETS(hp_str_new(&c, 1));
}
hval hpbi_bin2hex(hval s) {
    hstr *x = hp_val_to_str(s);
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < x->len; i++) buf_printf(&b, "%02x", (unsigned char)x->data[i]);
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_hex2bin(hval s) {
    hstr *x = hp_val_to_str(s);
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i + 1 < x->len; i += 2) {
        char hex[3] = { x->data[i], x->data[i + 1], 0 };
        buf_putc(&b, (char)strtol(hex, NULL, 16));
    }
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_str_split(hval s, hval n) {
    hstr *x = hp_val_to_str(s);
    int64_t k = hp_val_to_int(n);
    if (k <= 0) k = 1;
    harr *r = hp_arr_new();
    for (size_t i = 0; i < x->len; i += (size_t)k) {
        size_t l = x->len - i < (size_t)k ? x->len - i : (size_t)k;
        hp_arr_push(r, hp_of_str(hp_str_new(x->data + i, l)));
    }
    return hp_of_arr(r);
}
hval hpbi_ucwords(hval s) {
    hstr *r = hp_str_copy(hp_val_to_str(s));
    bool up = true;
    for (size_t i = 0; i < r->len; i++) {
        if (isspace((unsigned char)r->data[i])) up = true;
        else if (up) { r->data[i] = (char)toupper((unsigned char)r->data[i]); up = false; }
    }
    RETS(r);
}
hval hpbi_wordwrap(hval s, hval w) {
    (void)w;
    RETS(hp_val_to_str(s));
}
hval hpbi_similar_text(hval a, hval b) {
    hstr *x = hp_val_to_str(a), *y = hp_val_to_str(b);
    size_t same = 0;
    for (size_t i = 0; i < x->len; i++)
        for (size_t j = 0; j < y->len; j++)
            if (x->data[i] == y->data[j]) { same++; break; }
    RETI((int64_t)same);
}
hval hpbi_levenshtein(hval a, hval b) {
    hstr *x = hp_val_to_str(a), *y = hp_val_to_str(b);
    size_t n = x->len, m = y->len;
    static size_t dp[512];
    if (m + 1 > 512) RETI(-1);
    for (size_t j = 0; j <= m; j++) dp[j] = j;
    for (size_t i = 1; i <= n; i++) {
        size_t prev = dp[0], cur;
        dp[0] = i;
        for (size_t j = 1; j <= m; j++) {
            cur = dp[j];
            size_t cost = x->data[i - 1] == y->data[j - 1] ? 0 : 1;
            dp[j] = dp[j - 1] + 1 < dp[j] + 1 ? dp[j - 1] + 1 : dp[j] + 1;
            if (prev + cost < dp[j]) dp[j] = prev + cost;
            prev = cur;
        }
    }
    RETI((int64_t)dp[m]);
}

/* ---------- arrays ---------- */
hval hpbi_array_keys(hval a) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    return hp_of_arr(hp_arr_keys(a.u.a));
}
hval hpbi_array_values(hval a) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    return hp_of_arr(hp_arr_values(a.u.a));
}
hval hpbi_array_merge(hval a, hval b) {
    if (a.tag != HV_ARR || b.tag != HV_ARR) return a;
    return hp_of_arr(hp_arr_merge(a.u.a, b.u.a));
}
hval hpbi_array_slice(hval a, hval off, hval len) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    harr *x = a.u.a;
    int64_t o = hp_val_to_int(off);
    o = o < 0 ? o + (int64_t)x->len : o;
    int64_t l = len.tag == HV_NULL ? (int64_t)x->len - o : hp_val_to_int(len);
    if (l < 0) l = (int64_t)x->len - o + l;
    if (l < 0) l = 0;
    return hp_of_arr(hp_arr_slice(x, o, o + l));
}
hval hpbi_array_reverse(hval a) {
    if (a.tag != HV_ARR) return a;
    return hp_of_arr(hp_arr_reverse(a.u.a));
}
hval hpbi_array_sum(hval a) {
    if (a.tag != HV_ARR) RETI(0);
    return hp_arr_sum(a.u.a);
}
hval hpbi_array_product(hval a) {
    if (a.tag != HV_ARR) RETI(0);
    double p = 1.0;
    harr *x = a.u.a;
    for (size_t i = 0; i < x->len; i++) p *= hp_val_to_float(x->vals[i]);
    return hp_of_float(p);
}
hval hpbi_array_unique(hval a) {
    if (a.tag != HV_ARR) return a;
    return hp_of_arr(hp_arr_unique(a.u.a));
}
hval hpbi_in_array(hval needle, hval hay) {
    if (hay.tag != HV_ARR) RETB(false);
    RETB(hp_arr_in(hay.u.a, needle));
}
hval hpbi_array_search(hval needle, hval hay) {
    if (hay.tag != HV_ARR) RETB(false);
    harr *x = hay.u.a;
    for (size_t i = 0; i < x->len; i++)
        if (hp_val_eq(x->vals[i], needle))
            return x->is_map ? x->keys[i] : hp_of_int((int64_t)i);
    RETB(false);
}
hval hpbi_array_key_exists(hval k, hval a) {
    if (a.tag != HV_ARR) RETB(false);
    RETB(hp_arr_has(a.u.a, k));
}
hval hpbi_isset(hval v) { RETB(v.tag != HV_NULL); }
hval hpbi_range(hval lo, hval hi) {
    return hp_of_arr(hp_range(hp_val_to_int(lo), hp_val_to_int(hi)));
}
hval hpbi_array_map(hval fn, hval a) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    harr *r = hp_arr_new();
    for (size_t i = 0; i < a.u.a->len; i++) {
        hval v = a.u.a->vals[i];
        hval arg = hp_of_arr(hp_arr_of(1, &v));
        hval out = (fn.tag == HV_CLO) ? hp_closure_call(fn.u.p, 1, &arg) : hp_null;
        hp_arr_push(r, out);
    }
    return hp_of_arr(r);
}
hval hpbi_array_filter(hval a, hval fn) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    harr *r = hp_arr_new();
    for (size_t i = 0; i < a.u.a->len; i++) {
        hval v = a.u.a->vals[i];
        hval arg = hp_of_arr(hp_arr_of(1, &v));
        hval keep = (fn.tag == HV_CLO) ? hp_closure_call(fn.u.p, 1, &arg) : hp_null;
        if (hp_val_to_bool(keep))
            hp_arr_push(r, v);
    }
    return hp_of_arr(r);
}
hval hpbi_array_reduce(hval a, hval fn, hval init) {
    if (a.tag != HV_ARR) return init;
    hval acc = init;
    for (size_t i = 0; i < a.u.a->len; i++) {
        harr *args = hp_arr_of(2, (const hval[]){acc, a.u.a->vals[i]});
        hval av = hp_of_arr(args);
        acc = (fn.tag == HV_CLO) ? hp_closure_call(fn.u.p, 1, &av) : acc;
    }
    return acc;
}
hval hpbi_array_flip(hval a) {
    harr *r = hp_arr_new();
    r->is_map = true;
    if (a.tag == HV_ARR) {
        harr *x = a.u.a;
        for (size_t i = 0; i < x->len; i++)
            hp_arr_set(r, x->vals[i], x->is_map ? x->keys[i] : hp_of_int((int64_t)i));
    }
    return hp_of_arr(r);
}
hval hpbi_array_fill(hval start, hval n, hval v) {
    harr *r = hp_arr_new();
    int64_t st = hp_val_to_int(start), cnt = hp_val_to_int(n);
    for (int64_t i = 0; i < cnt; i++)
        hp_arr_set(r, hp_of_int(st + i), v);
    return hp_of_arr(r);
}
hval hpbi_array_combine(hval keys, hval vals) {
    harr *r = hp_arr_new();
    r->is_map = true;
    if (keys.tag == HV_ARR && vals.tag == HV_ARR) {
        harr *k = keys.u.a, *v = vals.u.a;
        size_t n = k->len < v->len ? k->len : v->len;
        for (size_t i = 0; i < n; i++)
            hp_arr_set(r, k->vals[i], v->vals[i]);
    }
    return hp_of_arr(r);
}
hval hpbi_array_diff(hval a, hval b) {
    harr *r = hp_arr_new();
    if (a.tag == HV_ARR) {
        for (size_t i = 0; i < a.u.a->len; i++)
            if (b.tag != HV_ARR || !hp_arr_in(b.u.a, a.u.a->vals[i]))
                hp_arr_push(r, a.u.a->vals[i]);
    }
    return hp_of_arr(r);
}
hval hpbi_array_intersect(hval a, hval b) {
    harr *r = hp_arr_new();
    if (a.tag == HV_ARR && b.tag == HV_ARR) {
        for (size_t i = 0; i < a.u.a->len; i++)
            if (hp_arr_in(b.u.a, a.u.a->vals[i]))
                hp_arr_push(r, a.u.a->vals[i]);
    }
    return hp_of_arr(r);
}
hval hpbi_array_push(hval a, hval v) {
    if (a.tag != HV_ARR) RETI(0);
    hp_arr_push(a.u.a, v);
    RETI((int64_t)a.u.a->len);
}
hval hpbi_array_pop(hval a) {
    if (a.tag != HV_ARR) return hp_null;
    return hp_arr_pop(a.u.a);
}
hval hpbi_end(hval a) {
    if (a.tag != HV_ARR || a.u.a->len == 0) return hp_null;
    return a.u.a->vals[a.u.a->len - 1];
}
hval hpbi_reset(hval a) {
    if (a.tag != HV_ARR || a.u.a->len == 0) return hp_null;
    return a.u.a->vals[0];
}
hval hpbi_array_shift(hval a) {
    if (a.tag != HV_ARR) return hp_null;
    return hp_arr_shift(a.u.a);
}
hval hpbi_array_unshift(hval a, hval v) {
    if (a.tag != HV_ARR) RETI(0);
    hp_arr_unshift(a.u.a, v);
    RETI((int64_t)a.u.a->len);
}
hval hpbi_array_splice(hval a, hval off, hval len) {
    if (a.tag != HV_ARR) return hp_of_arr(hp_arr_new());
    harr *removed = hp_arr_slice(a.u.a, hp_val_to_int(off), hp_val_to_int(off) + hp_val_to_int(len));
    return hp_of_arr(removed);
}
hval hpbi_shuffle(hval a) {
    if (a.tag != HV_ARR) return a;
    harr *r = hp_arr_clone(a.u.a);
    for (size_t i = r->len; i > 1; i--) {
        size_t j = (size_t)(rand() % (int64_t)i);
        hval t = r->vals[i - 1];
        r->vals[i - 1] = r->vals[j];
        r->vals[j] = t;
    }
    return hp_of_arr(r);
}

/* ---------- in-place sorts (sort/rsort/ksort/krsort/asort) ---------- */
/* PHP sorts mutate the array argument (arrays are handles). All sort
 * by hp_val_cmp; k* sorts by keys, others by values. Plain lists have
 * keys == NULL, so only touch keys when is_map (like PHP's sort(),
 * value sorts reindex to 0..n-1). */
/* qsort comparators — O(n log n) so 1M-element sorts stay fast */
static int hp_sgn64(int64_t x) { return x < 0 ? -1 : (x > 0 ? 1 : 0); }
static int hp_qcmp_val_asc(const void *pa, const void *pb) {
    return hp_sgn64(hp_val_cmp(*(const hval *)pa, *(const hval *)pb));
}
static int hp_qcmp_val_desc(const void *pa, const void *pb) {
    return -hp_qcmp_val_asc(pa, pb);
}

typedef struct { hval k, v; } hp_kv;

static int hp_sort_mode;
enum { HP_SORT_VAL_ASC, HP_SORT_VAL_DESC, HP_SORT_KEY_ASC, HP_SORT_KEY_DESC };

static int hp_kv_cmp(const void *pa, const void *pb) {
    const hp_kv *a = pa, *b = pb;
    switch (hp_sort_mode) {
    case HP_SORT_KEY_ASC:   return  hp_sgn64(hp_val_cmp(a->k, b->k));
    case HP_SORT_KEY_DESC:  return -hp_sgn64(hp_val_cmp(a->k, b->k));
    case HP_SORT_VAL_DESC:  return -hp_sgn64(hp_val_cmp(a->v, b->v));
    default:                return  hp_sgn64(hp_val_cmp(a->v, b->v));
    }
}
static int hp_sort_mode;

/* sort values, optionally keeping (key, value) pairs associated.
 * Used by sort/rsort (plain), asort/arsort (assoc by value),
 * ksort/krsort (assoc by key). */
static void hp_sort_pairs(harr *a, bool by_key, bool desc) {
    if (a->len < 2) return;
    bool has_keys = a->is_map && a->keys;
    if (!has_keys) {
        /* plain array: qsort the vals, keys stay implicit 0..n-1 */
        qsort(a->vals, a->len, sizeof(hval),
              desc ? hp_qcmp_val_desc : hp_qcmp_val_asc);
        return;
    }
    hp_sort_mode = by_key ? (desc ? HP_SORT_KEY_DESC : HP_SORT_KEY_ASC)
                          : (desc ? HP_SORT_VAL_DESC : HP_SORT_VAL_ASC);
    hp_kv *tmp = hp_alloc(a->len * sizeof(hp_kv));
    for (size_t i = 0; i < a->len; i++) {
        tmp[i].k = a->keys[i];
        tmp[i].v = a->vals[i];
    }
    qsort(tmp, a->len, sizeof(hp_kv), hp_kv_cmp);
    for (size_t i = 0; i < a->len; i++) {
        a->keys[i] = tmp[i].k;
        a->vals[i] = tmp[i].v;
    }
    free(tmp);
}
hval hpbi_sort(hval a)  { if (a.tag == HV_ARR) hp_sort_pairs(a.u.a, false, false); RETB(true); }
hval hpbi_rsort(hval a) { if (a.tag == HV_ARR) hp_sort_pairs(a.u.a, false, true);  RETB(true); }
hval hpbi_ksort(hval a) { if (a.tag == HV_ARR) hp_sort_pairs(a.u.a, true,  false); RETB(true); }
hval hpbi_krsort(hval a){ if (a.tag == HV_ARR) hp_sort_pairs(a.u.a, true,  true);  RETB(true); }
hval hpbi_asort(hval a) { if (a.tag == HV_ARR) hp_sort_pairs(a.u.a, false, false); RETB(true); }

/* ---------- math ---------- */
hval hpbi_max(hval a) {
    if (a.tag != HV_ARR) return a;
    harr *args = a.u.a;
    if (args->len == 0) return hp_null;
    hval best = args->vals[0];
    for (size_t i = 1; i < args->len; i++)
        if (hp_val_cmp(args->vals[i], best) > 0) best = args->vals[i];
    return best;
}
hval hpbi_min(hval a) {
    if (a.tag != HV_ARR) return a;
    harr *args = a.u.a;
    if (args->len == 0) return hp_null;
    hval best = args->vals[0];
    for (size_t i = 1; i < args->len; i++)
        if (hp_val_cmp(args->vals[i], best) < 0) best = args->vals[i];
    return best;
}
hval hpbi_abs(hval v) {
    if (v.tag == HV_FLOAT) RETF(fabs(v.u.f));
    int64_t i = hp_val_to_int(v);
    RETI(i < 0 ? -i : i);
}
hval hpbi_round(hval v, hval prec, hval mode) {
    /* PHP 8: round($x[, $precision[, $mode]]) — mode 1=HALF_DOWN,
     * 2=HALF_EVEN, 3=HALF_ODD; default HALF_UP. Presision mode halves-to-even.
     * PHP_ROUND_HALF_* constants map to 0..3 in builtins.c. */
    int m = mode.tag == HV_NULL ? 0 : (int)hp_val_to_int(mode);
    if (prec.tag == HV_NULL && m == 0) RETF(round(hp_val_to_float(v)));
    double x = hp_val_to_float(v);
    if (prec.tag == HV_NULL) {
        /* precision NULL but mode given: round to integer with mode */
        double fl = floor(x), diff = x - fl;
        if (diff > 0.5) x = fl + 1;
        else if (diff < 0.5) x = fl;
        else if (m == 2) x = (fmod(fl, 2.0) == 0) ? fl : fl + 1;
        else if (m == 3) x = (fmod(fl, 2.0) != 0) ? fl : fl + 1;
        else if (m == 1) x = fl;
        else x = fl + 1;
        RETF(x);
    }
    double p = pow(10.0, hp_val_to_float(prec));
    double y = x * p;
    double fl = floor(y), diff = y - fl;
    if (diff > 0.5) y = fl + 1;
    else if (diff < 0.5) y = fl;
    else if (m == 2) y = (fmod(fl, 2.0) == 0) ? fl : fl + 1;
    else if (m == 3) y = (fmod(fl, 2.0) != 0) ? fl : fl + 1;
    else if (m == 1) y = fl;
    else y = fl + 1;
    RETF(y / p);
}
hval hpbi_floor(hval v) { RETF(floor(hp_val_to_float(v))); }
hval hpbi_ceil(hval v) { RETF(ceil(hp_val_to_float(v))); }
hval hpbi_sqrt(hval v) { RETF(sqrt(hp_val_to_float(v))); }
hval hpbi_pow(hval a, hval b) { return hp_powv(a, b); }
hval hpbi_intdiv(hval a, hval b) {
    int64_t d = hp_val_to_int(b);
    if (d == 0) hp_throw_str("Division by zero");
    RETI(hp_val_to_int(a) / d);
}
hval hpbi_fmod(hval a, hval b) { RETF(fmod(hp_val_to_float(a), hp_val_to_float(b))); }
hval hpbi_sin(hval v) { RETF(sin(hp_val_to_float(v))); }
hval hpbi_cos(hval v) { RETF(cos(hp_val_to_float(v))); }
hval hpbi_tan(hval v) { RETF(tan(hp_val_to_float(v))); }
hval hpbi_atan(hval v) { RETF(atan(hp_val_to_float(v))); }
hval hpbi_atan2(hval a, hval b) { RETF(atan2(hp_val_to_float(a), hp_val_to_float(b))); }
hval hpbi_asin(hval v) { RETF(asin(hp_val_to_float(v))); }
hval hpbi_acos(hval v) { RETF(acos(hp_val_to_float(v))); }
hval hpbi_log(hval v) { RETF(log(hp_val_to_float(v))); }
hval hpbi_log2(hval v) { RETF(log2(hp_val_to_float(v))); }
hval hpbi_log10(hval v) { RETF(log10(hp_val_to_float(v))); }
hval hpbi_exp(hval v) { RETF(exp(hp_val_to_float(v))); }
hval hpbi_is_nan(hval v) { RETB(isnan(hp_val_to_float(v))); }
hval hpbi_is_finite(hval v) { RETB(isfinite(hp_val_to_float(v))); }
hval hpbi_is_infinite(hval v) { RETB(isinf(hp_val_to_float(v))); }
hval hpbi_pi(void) { RETF(3.14159265358979323846); }
hval hpbi_deg2rad(hval v) { RETF(hp_val_to_float(v) * 0.017453292519943295); }
hval hpbi_rad2deg(hval v) { RETF(hp_val_to_float(v) * 57.29577951308232); }
hval hpbi_hypot(hval a, hval b) { RETF(hypot(hp_val_to_float(a), hp_val_to_float(b))); }

/* ---------- type tests ---------- */
hval hpbi_is_int(hval v) { RETB(v.tag == HV_INT); }
hval hpbi_is_integer(hval v) { RETB(v.tag == HV_INT); }
hval hpbi_is_float(hval v) { RETB(v.tag == HV_FLOAT); }
hval hpbi_is_string(hval v) { RETB(v.tag == HV_STR); }
hval hpbi_is_bool(hval v) { RETB(v.tag == HV_BOOL); }
hval hpbi_is_array(hval v) { RETB(v.tag == HV_ARR); }
hval hpbi_is_null(hval v) { RETB(v.tag == HV_NULL); }
hval hpbi_is_numeric(hval v) { RETB(hp_num_like(v)); }
hval hpbi_intval(hval v) { RETI(hp_val_to_int(v)); }
hval hpbi_floatval(hval v) { RETF(hp_val_to_float(v)); }
hval hpbi_doubleval(hval v) { RETF(hp_val_to_float(v)); }
hval hpbi_strval(hval v) { RETS(hp_val_to_str(v)); }
hval hpbi_boolval(hval v) { RETB(hp_val_to_bool(v)); }
hval hpbi_gettype(hval v) { RETS(hp_str_lit(hp_kind_name(v.tag))); }

/* ---------- dump ---------- */
static void hp_dump_r(hval v, Buf *b, int depth);
static void hp_dump_indent(Buf *b, int depth) {
    for (int i = 0; i < depth; i++) buf_puts(b, "  ");
}
static void hp_dump_r(hval v, Buf *b, int depth) {
    switch (v.tag) {
    case HV_NULL: buf_puts(b, "NULL"); break;
    case HV_INT: buf_printf(b, "int(%lld)", (long long)v.u.i); break;
    case HV_FLOAT: buf_printf(b, "float(%g)", v.u.f); break;
    case HV_BOOL: buf_puts(b, v.u.b ? "bool(true)" : "bool(false)"); break;
    case HV_STR: buf_printf(b, "string(%zu) \"%.*s\"", v.u.s->len, (int)v.u.s->len, v.u.s->data); break;
    case HV_ARR: {
        harr *a = v.u.a;
        buf_printf(b, "array(%zu) {\n", a->len);
        for (size_t i = 0; i < a->len; i++) {
            hp_dump_indent(b, depth + 1);
            if (a->is_map) {
                hval k = a->keys[i];
                if (k.tag == HV_STR) buf_printf(b, "[\"%.*s\"]=>\n", (int)k.u.s->len, k.u.s->data);
                else buf_printf(b, "[%lld]=>\n", (long long)k.u.i);
            } else buf_printf(b, "[%d]=>\n", (int)i);
            hp_dump_indent(b, depth + 1);
            hp_dump_r(a->vals[i], b, depth + 1);
            buf_puts(b, "\n");
        }
        hp_dump_indent(b, depth);
        buf_puts(b, "}");
        break;
    }
    case HV_OBJ: buf_printf(b, "object(%p)", v.u.p); break;
    case HV_CLO: buf_puts(b, "object(Closure)"); break;
    }
}
hval hpbi_var_dump(hval v) {
    Buf b;
    buf_init(&b);
    hp_dump_r(v, &b, 0);
    buf_puts(&b, "\n");
    hstr *s = hp_str_lit(buf_take(&b));
    fwrite(s->data, 1, s->len, stdout);
    RETI(0);
}
static void hp_print_r_r(hval v, Buf *b, int depth) {
    if (v.tag == HV_ARR) {
        harr *a = v.u.a;
        buf_puts(b, "Array\n");
        hp_dump_indent(b, depth);
        buf_puts(b, "(\n");
        for (size_t i = 0; i < a->len; i++) {
            hp_dump_indent(b, depth + 1);
            if (a->is_map) {
                hval k = a->keys[i];
                if (k.tag == HV_STR) buf_printf(b, "[%.*s] => ", (int)k.u.s->len, k.u.s->data);
                else buf_printf(b, "[%lld] => ", (long long)k.u.i);
            } else buf_printf(b, "[%d] => ", (int)i);
            if (a->vals[i].tag == HV_ARR) {
                hp_print_r_r(a->vals[i], b, depth + 1);
                buf_puts(b, "\n");
            } else {
                hp_print_r_r(a->vals[i], b, depth + 1);
                buf_puts(b, "\n");
            }
        }
        hp_dump_indent(b, depth);
        buf_puts(b, ")\n");
    } else {
        hstr *s = hp_val_to_str(v);
        buf_printf(b, "%.*s", (int)s->len, s->data);
    }
}
hval hpbi_print_r(hval v) {
    /* PHP semantics: print_r prints and returns true when called as a
     * statement; HolyPHP always prints and additionally returns the text so
     * string context ("..." . print_r($x)) also works. */
    Buf b;
    buf_init(&b);
    hp_print_r_r(v, &b, 0);
    hstr *s = hp_str_lit(buf_take(&b));
    fwrite(s->data, 1, s->len, stdout);
    RETS(s);
}
hval hpbi_var_export(hval v) {
    Buf b;
    buf_init(&b);
    hp_print_r_r(v, &b, 0);
    hstr *s = hp_str_lit(buf_take(&b));
    fwrite(s->data, 1, s->len, stdout);
    RETS(s);
}
hval hpbi_serialize(hval v) { return hpbi_print_r(v); }
hval hpbi_unserialize(hval s) { (void)s; return hp_null; }

/* ---------- io / process ---------- */
hval hpbi_readline(hval prompt) {
    if (prompt.tag == HV_STR && prompt.u.s->len)
        fwrite(prompt.u.s->data, 1, prompt.u.s->len, stdout);
    char buf[4096];
    if (!fgets(buf, sizeof buf, stdin)) RETS(hp_str_lit(""));
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    RETS(hp_str_new(buf, n));
}
hval hpbi_file_get_contents(hval path) {
    hstr *p = hp_val_to_str(path);
    FILE *f = fopen(p->data, "rb");
    if (!f) {
        hp_throw_str("failed to open stream");
        RETB(false);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = hp_alloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    RETS(hp_str_new(buf, got));
}
hval hpbi_file_put_contents(hval path, hval data, hval flags) {
    /* PHP: flags & FILE_APPEND (8) appends instead of truncating; an array
     * of values is written piece by piece (PHP 7.8+ style). */
    hstr *p = hp_val_to_str(path);
    const char *how = (flags.tag == HV_INT && (flags.u.i & 8)) ? "ab" : "wb";
    if (data.tag == HV_ARR) {
        FILE *f = fopen(p->data, how);
        if (!f) RETB(false);
        harr *a = data.u.a;
        for (size_t i = 0; i < a->len; i++) {
            hstr *s = hp_val_to_str(a->vals[i]);
            fwrite(s->data, 1, s->len, f);
        }
        fclose(f);
        RETB(true);
    }
    hstr *d = hp_val_to_str(data);
    FILE *f = fopen(p->data, how);
    if (!f) RETB(false);
    size_t n = fwrite(d->data, 1, d->len, f);
    fclose(f);
    RETI((int64_t)n);
}
hval hpbi_getenv(hval name) {
    const char *v = getenv(hp_val_to_str(name)->data);
    RETS(v ? hp_str_lit(v) : hp_str_lit(""));
}
hval hpbi_putenv(hval kv) { RETB(putenv(hp_val_to_str(kv)->data) == 0); }
hval hpbi_php_uname(void) {
#if defined(_WIN32)
    RETS(hp_str_lit("Windows NT"));
#elif defined(__APPLE__)
    RETS(hp_str_lit("Darwin"));
#elif defined(__unix__)
    RETS(hp_str_lit("Linux"));
#else
    RETS(hp_str_lit("Unknown"));
#endif
}
hval hpbi_phpversion(void) { RETS(hp_str_lit(HPHP_VERSION)); }
hval hpbi_php_sapi_name(void) { RETS(hp_str_lit("cli")); }
hval hpbi_microtime(void) {
    /* High-resolution on all platforms: PHP's microtime(true) needs
     * sub-second precision for benchmarks. */
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    RETF((double)c.QuadPart / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    RETF((double)ts.tv_sec + ts.tv_nsec / 1e9);
#endif
}
hval hpbi_hrtime(hval as_number) {
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    int64_t ns = (int64_t)((double)c.QuadPart / (double)freq.QuadPart * 1e9);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ns = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
#endif
    (void)as_number;
    RETI(ns);
}
static size_t hp_mem_used = 0;
hval hpbi_memory_get_usage(void) { RETI((int64_t)hp_mem_used); }
hval hpbi_memory_get_peak_usage(void) { RETI((int64_t)hp_mem_used); }
hval hpbi_gc_collect_cycles(void) { RETI(0); }
hval hpbi_date(hval fmt) {
    /* PHP date(): Y m d H i s and friends over the local time */
    time_t now = time(NULL);
    struct tm tmbuf;
#if defined(_WIN32)
    localtime_s(&tmbuf, &now);
#else
    localtime_r(&now, &tmbuf);
#endif
    struct tm *tmv = &tmbuf;
    hstr *f = hp_val_to_str(fmt);
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < f->len; i++) {
        char c = f->data[i];
        if (c == '\\') { if (i + 1 < f->len) buf_putc(&b, f->data[++i]); continue; }
        if (c == 'Y') { buf_printf(&b, "%04d", tmv->tm_year + 1900); continue; }
        if (c == 'y') { buf_printf(&b, "%02d", (tmv->tm_year + 1900) % 100); continue; }
        if (c == 'm') { buf_printf(&b, "%02d", tmv->tm_mon + 1); continue; }
        if (c == 'n') { buf_printf(&b, "%d", tmv->tm_mon + 1); continue; }
        if (c == 'd') { buf_printf(&b, "%02d", tmv->tm_mday); continue; }
        if (c == 'j') { buf_printf(&b, "%d", tmv->tm_mday); continue; }
        if (c == 'H') { buf_printf(&b, "%02d", tmv->tm_hour); continue; }
        if (c == 'G') { buf_printf(&b, "%d", tmv->tm_hour); continue; }
        if (c == 'i') { buf_printf(&b, "%02d", tmv->tm_min); continue; }
        if (c == 's') { buf_printf(&b, "%02d", tmv->tm_sec); continue; }
        if (c == 'h') { int hh = tmv->tm_hour % 12; if (hh == 0) hh = 12; buf_printf(&b, "%02d", hh); continue; }
        if (c == 'g') { int hh = tmv->tm_hour % 12; if (hh == 0) hh = 12; buf_printf(&b, "%d", hh); continue; }
        if (c == 'A') { buf_puts(&b, tmv->tm_hour < 12 ? "AM" : "PM"); continue; }
        if (c == 'a') { buf_puts(&b, tmv->tm_hour < 12 ? "am" : "pm"); continue; }
        if (c == 'w') { buf_printf(&b, "%d", tmv->tm_wday); continue; }
        if (c == 'N') { buf_printf(&b, "%d", tmv->tm_wday == 0 ? 7 : tmv->tm_wday); continue; }
        if (c == 'z') { buf_printf(&b, "%d", tmv->tm_yday); continue; }
        if (c == 'L') { int y = tmv->tm_year + 1900; buf_printf(&b, "%d", ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)); continue; }
        if (c == 't') { static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31}; int mm = tmv->tm_mon; int d = mdays[mm]; if (mm == 1) { int y = tmv->tm_year + 1900; if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) d = 29; } buf_printf(&b, "%d", d); continue; }
        if (c == 'U') { buf_printf(&b, "%lld", (long long)now); continue; }
        buf_putc(&b, c);
    }
    RETS(hp_str_lit(buf_take(&b)));
}
hval hpbi_time(void) { RETI((int64_t)time(NULL)); }
hval hpbi_usleep(hval us) {
#if defined(_WIN32)
    Sleep((DWORD)(hp_val_to_int(us) / 1000));
#else
    struct timespec ts = { .tv_sec = 0, .tv_nsec = hp_val_to_int(us) * 1000 };
    nanosleep(&ts, NULL);
#endif
    return hp_null;
}
hval hpbi_sleep(hval s) {
#if defined(_WIN32)
    (void)s;
#else
    struct timespec ts = { .tv_sec = hp_val_to_int(s), .tv_nsec = 0 };
    nanosleep(&ts, NULL);
#endif
    RETI(0);
}
static void hp_seed_once(void) {
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
}
hval hpbi_rand(hval mn, hval mx) {
    hp_seed_once();
    if (mn.tag != HV_NULL || mx.tag != HV_NULL)
        RETI(hp_val_to_int(mn) + (int64_t)rand() %
             (hp_val_to_int(mx) - hp_val_to_int(mn) + 1));
    RETI((int64_t)rand());
}
hval hpbi_mt_rand(hval mn, hval mx) {
    hp_seed_once();
    if (mn.tag != HV_NULL || mx.tag != HV_NULL)
        RETF((double)hp_val_to_int(mn) +
             ((double)rand() / (double)RAND_MAX) *
                 (double)(hp_val_to_int(mx) - hp_val_to_int(mn)));
    RETF((double)rand() / (double)RAND_MAX);
}
hval hpbi_srand(hval seed) { srand((unsigned)hp_val_to_int(seed)); return hp_null; }
hval hpbi_mt_srand(hval seed) { srand((unsigned)hp_val_to_int(seed)); return hp_null; }
hval hpbi_random_int(hval mn, hval mx) { return hpbi_rand(mn, mx); }
hval hpbi_fgetc(hval stream) {
    (void)stream;
    int c = fgetc(stdin);
    if (c == EOF) RETS(hp_str_lit(""));
    char b[1] = { (char)c };
    RETS(hp_str_new(b, 1));
}

/* ---------- filesystem helpers ---------- */
hval hpbi_getcwd(void) {
    char buf[2048];
#if defined(_WIN32)
    GetCurrentDirectoryA(sizeof buf, buf);
#else
    if (!getcwd(buf, sizeof buf)) buf[0] = 0;
#endif
    RETS(hp_str_new(buf, strlen(buf)));
}
hval hpbi_file_exists(hval path) {
    hstr *p = hp_val_to_str(path);
    FILE *f = fopen(p->data, "rb");
    if (f) { fclose(f); RETB(true); }
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(p->data);
    RETB(attr != INVALID_FILE_ATTRIBUTES);
#else
    RETB(access(p->data, F_OK) == 0);
#endif
}
hval hpbi_is_dir(hval path) {
    hstr *p = hp_val_to_str(path);
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(p->data);
    RETB(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    RETB(stat(p->data, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}
hval hpbi_filesize(hval path) {
    hstr *p = hp_val_to_str(path);
    FILE *f = fopen(p->data, "rb");
    if (!f) RETI(-1);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    RETI(sz);
}
hval hpbi_mkdir(hval path) {
    hstr *p = hp_val_to_str(path);
#if defined(_WIN32)
    RETB(mkdir(p->data) == 0);
#else
    RETB(mkdir(p->data, 0777) == 0);
#endif
}
hval hpbi_rmdir(hval path) { RETB(rmdir(hp_val_to_str(path)->data) == 0); }
hval hpbi_unlink(hval path) { RETB(remove(hp_val_to_str(path)->data) == 0); }
hval hpbi_rename(hval from, hval to) {
    RETB(rename(hp_val_to_str(from)->data, hp_val_to_str(to)->data) == 0);
}
hval hpbi_copy(hval from, hval to) {
    FILE *a = fopen(hp_val_to_str(from)->data, "rb");
    if (!a) RETB(false);
    FILE *b = fopen(hp_val_to_str(to)->data, "wb");
    if (!b) { fclose(a); RETB(false); }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
    RETB(true);
}
/* fgets/fwrite now live with the file-handle block (fopen/fclose below):
 * they dispatch on int handle (new) vs string path (legacy). */
/* ---------- real file handles ----------
 * fopen() returns a small integer handle; fgets/fwrite/fread/feof/fclose
 * operate on it. The old fopen("...") returned a raw pointer in an int
 * and fclose was a no-op — leaking every file.
 * fwrite($h, ...) / fgets($h) dispatch: int handle = new-style, string
 * path = legacy whole-file read / append-write. */
#define HP_MAX_FILES 512
static FILE *g_files[HP_MAX_FILES];
static bool g_file_open[HP_MAX_FILES];

hval hpbi_fopen(hval path, hval mode) {
    hstr *p = hp_val_to_str(path), *m = hp_val_to_str(mode);
    FILE *f = fopen(p->data, m->data);
    if (!f) RETB(false);
    for (int i = 1; i < HP_MAX_FILES; i++) {
        if (!g_file_open[i]) {
            g_files[i] = f;
            g_file_open[i] = true;
            RETI(i);
        }
    }
    fclose(f);
    RETB(false);
}
static bool file_ok(hval fh) {
    return fh.tag == HV_INT && fh.u.i > 0 && fh.u.i < HP_MAX_FILES &&
           g_file_open[fh.u.i];
}
hval hpbi_fgets(hval fh) {
    if (fh.tag == HV_STR) {
        /* legacy: fgets("path.txt") = first line of the file */
        FILE *f = fopen(hp_val_to_str(fh)->data, "rb");
        if (!f) RETS(hp_str_lit(""));
        char line[4096];
        if (!fgets(line, sizeof line, f)) { fclose(f); RETS(hp_str_lit("")); }
        fclose(f);
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
        RETS(hp_str_new(line, n));
    }
    if (!file_ok(fh)) RETB(false);            /* PHP: fgets at EOF => false */
    char line[4096];
    if (!fgets(line, sizeof line, g_files[fh.u.i])) RETB(false);
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    RETS(hp_str_new(line, n));
}
hval hpbi_fwrite(hval fh, hval data) {
    if (fh.tag == HV_STR) {
        /* legacy: fwrite("path", data) appends to the file */
        hstr *p = hp_val_to_str(fh), *d = hp_val_to_str(data);
        FILE *f = fopen(p->data, "ab");
        if (!f) RETI(0);
        size_t n = fwrite(d->data, 1, d->len, f);
        fclose(f);
        RETI((int64_t)n);
    }
    if (!file_ok(fh)) RETI(0);
    hstr *d = hp_val_to_str(data);
    RETI((int64_t)fwrite(d->data, 1, d->len, g_files[fh.u.i]));
}
hval hpbi_fread(hval fh, hval n) {
    if (!file_ok(fh)) RETS(hp_str_lit(""));
    int64_t want = hp_val_to_int(n);
    if (want <= 0) RETS(hp_str_lit(""));
    if (want > 1 << 20) want = 1 << 20;
    char *buf = (char *)malloc((size_t)want);
    size_t got = fread(buf, 1, (size_t)want, g_files[fh.u.i]);
    hstr *r = hp_str_new(buf, got);
    free(buf);
    RETS(r);
}
hval hpbi_feof(hval fh) {
    if (!file_ok(fh)) RETB(true);
    RETB(feof(g_files[fh.u.i]) != 0);
}
hval hpbi_fclose(hval fh) {
    if (!file_ok(fh)) RETB(false);
    fclose(g_files[fh.u.i]);
    g_file_open[fh.u.i] = false;
    g_files[fh.u.i] = NULL;
    RETB(true);
}
hval hpbi_scandir(hval path) {
    hstr *p = hp_val_to_str(path);
    harr *out = hp_arr_new();
#if defined(_WIN32)
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s\\*", p->data);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return hp_of_arr(out);
    do {
        if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
            hp_arr_push(out, hp_of_str(hp_str_new(fd.cFileName, strlen(fd.cFileName))));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(p->data);
    if (!d) return hp_of_arr(out);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
            hp_arr_push(out, hp_of_str(hp_str_new(ent->d_name, strlen(ent->d_name))));
    }
    closedir(d);
#endif
    return hp_of_arr(out);
}

/* ---------- number base conversion ---------- */
hval hpbi_decbin(hval n) { char b[80]; int64_t v = hp_val_to_int(n); itoa_bin(v, 2, b); RETS(hp_str_new(b, strlen(b))); }
hval hpbi_decoct(hval n) { char b[80]; int64_t v = hp_val_to_int(n); itoa_bin(v, 8, b); RETS(hp_str_new(b, strlen(b))); }
hval hpbi_dechex(hval n) { char b[80]; int64_t v = hp_val_to_int(n); itoa_bin(v, 16, b); RETS(hp_str_new(b, strlen(b))); }
static int64_t from_base(const char *s, int base) {
    return (int64_t)strtoll(s, NULL, base);
}
hval hpbi_bindec(hval s) { RETI(from_base(hp_val_to_str(s)->data, 2)); }
hval hpbi_octdec(hval s) { RETI(from_base(hp_val_to_str(s)->data, 8)); }
hval hpbi_hexdec(hval s) { RETI(from_base(hp_val_to_str(s)->data, 16)); }

/* ---------- unsafe memory ops ---------- */
hval hpbi_malloc(hval n) {
    void *p = malloc((size_t)hp_val_to_int(n));
    return hp_of_ptr(p);
}
hval hpbi_free(hval p) {
    if (p.tag == HV_OBJ) free(p.u.p);
    return hp_null;
}
hval hpbi_memcpy(hval dst, hval src, hval n) {
    if (dst.tag == HV_OBJ && src.tag == HV_OBJ)
        memcpy(dst.u.p, src.u.p, (size_t)hp_val_to_int(n));
    return dst;
}
hval hpbi_memset(hval dst, hval byte, hval n) {
    if (dst.tag == HV_OBJ) memset(dst.u.p, (int)hp_val_to_int(byte), (size_t)hp_val_to_int(n));
    return dst;
}
hval hpbi_ffi_load(hval lib) {
    (void)lib;
    hp_throw_str("ffi_load() is not available in this build");
    return hp_null;
}
hval hpbi_ffi_call(hval fn, hval args) {
    (void)fn; (void)args;
    hp_throw_str("ffi_call() is not available in this build");
    return hp_null;
}
hval hpbi_heap_dump(hval p) {
    char buf[64];
    snprintf(buf, sizeof buf, "%p", p.tag == HV_OBJ ? p.u.p : NULL);
    RETS(hp_str_lit(buf));
}

/* helper: scalar fallback ops used by codegen for mixed arithmetic */
hval hpbi_scalar_sub(hval a, hval b) { return hp_sub(a, b); }
hval hpbi_scalar_mul(hval a, hval b) { return hp_mul(a, b); }
hval hpbi_scalar_binop(hval a, hval b) { return hp_null; }

/* exit()/die(): stop the program right now (PHP semantics) */
hval hpbi_exit(hval code) {
    fflush(stdout);
    exit((int)hp_val_to_int(code));
    return hp_null;
}
hval hpbi_die(hval code) { return hpbi_exit(code); }

/* ------------------------------------------------------------------ */
/* sockets — the transport layer PHP calls "streams"                   */
/*                                                                     */
/*   $srv = stream_socket_server("tcp://127.0.0.1:9001");  -> handle   */
/*   $c   = stream_socket_client("tcp://127.0.0.1:9001");  -> handle   */
/*   $c   = stream_socket_accept($srv);        -> handle, -1 if none   */
/*   stream_set_blocking($c, false);           -> bool                 */
/*   stream_ready(array $handles, int $ms);    -> the readable ones    */
/*   stream_recv($c, int $max);                -> string, "" = nothing */
/*   stream_send($c, string $data);            -> bytes sent           */
/*   stream_eof($c); stream_peer($c); stream_close($c);                */
/*                                                                     */
/* Handles are plain integers (PHP exposes fds the same way) and every  */
/* accepted socket is non-blocking, so a server loop polls with         */
/* stream_ready() and one slow client can never stall the others.       */
/* ------------------------------------------------------------------ */

static bool hp_net_up = false;

static void hp_net_init(void) {
    if (hp_net_up) return;
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    hp_net_up = true;
}

/* Per-socket state. EOF is sticky: once the peer closes, it stays closed
 * even if we stop reading. */
typedef struct { intptr_t h; bool eof; } hp_sockrec;
static hp_sockrec *g_sock;
static size_t g_sock_len, g_sock_cap;

static hp_sockrec *sockrec(intptr_t h, bool create) {
    for (size_t i = 0; i < g_sock_len; i++)
        if (g_sock[i].h == h) return &g_sock[i];
    if (!create) return NULL;
    if (g_sock_len == g_sock_cap) {
        g_sock_cap = g_sock_cap ? g_sock_cap * 2 : 16;
        g_sock = (hp_sockrec *)realloc(g_sock, g_sock_cap * sizeof(hp_sockrec));
        if (!g_sock) { fprintf(stderr, "hphp: out of memory\n"); exit(1); }
    }
    g_sock[g_sock_len].h = h;
    g_sock[g_sock_len].eof = false;
    return &g_sock[g_sock_len++];
}

static void sock_forget(intptr_t h) {
    for (size_t i = 0; i < g_sock_len; i++)
        if (g_sock[i].h == h) { g_sock[i] = g_sock[--g_sock_len]; return; }
}

static hp_sock sock_of(hval v) { return (hp_sock)(intptr_t)hp_val_to_int(v); }

static void sock_nonblocking(hp_sock s, bool nonblock) {
#ifdef _WIN32
    u_long v = nonblock ? 1 : 0;
    ioctlsocket(s, FIONBIO, &v);
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return;
    fcntl(s, F_SETFL, nonblock ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
#endif
}

static void sock_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* "tcp://host:port" | "host:port" | ":port" */
static bool sock_split_addr(hstr *addr, char *host, size_t hsz, char *port, size_t psz) {
    const char *p = addr ? addr->data : "";
    const char *scheme = strstr(p, "://");
    if (scheme) p = scheme + 3;
    const char *colon = strrchr(p, ':');
    if (!colon) return false;
    size_t hl = (size_t)(colon - p);
    if (hl >= hsz) hl = hsz - 1;
    memcpy(host, p, hl);
    host[hl] = 0;
    snprintf(port, psz, "%s", colon + 1);
    return port[0] != 0;
}

hval hpbi_stream_socket_server(hval addr) {
    hp_net_init();
    char host[256], port[32];
    if (!sock_split_addr(hp_val_to_str(addr), host, sizeof host, port, sizeof port))
        return hp_of_int(-1);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(host[0] ? host : NULL, port, &hints, &res) != 0 || !res)
        return hp_of_int(-1);
    hp_sock s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == HP_SOCK_BAD) { freeaddrinfo(res); return hp_of_int(-1); }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    if (bind(s, res->ai_addr, (int)res->ai_addrlen) != 0 || listen(s, 128) != 0) {
        freeaddrinfo(res);
        hp_sock_close(s);
        return hp_of_bool(false);   /* PHP-style: failure is falsy */
    }
    freeaddrinfo(res);
    sock_nonblocking(s, true);
    sockrec((intptr_t)s, true);
    return hp_of_int((int64_t)(intptr_t)s);
}

hval hpbi_stream_socket_client(hval addr) {
    hp_net_init();
    char host[256], port[32];
    if (!sock_split_addr(hp_val_to_str(addr), host, sizeof host, port, sizeof port))
        return hp_of_bool(false);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host[0] ? host : "127.0.0.1", port, &hints, &res) != 0 || !res)
        return hp_of_bool(false);
    hp_sock s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == HP_SOCK_BAD) { freeaddrinfo(res); return hp_of_bool(false); }
    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) { hp_sock_close(s); return hp_of_bool(false); }
    sock_nonblocking(s, true);
    sockrec((intptr_t)s, true);
    return hp_of_int((int64_t)(intptr_t)s);
}

hval hpbi_stream_socket_accept(hval srv) {
    hp_net_init();
    hp_sock s = sock_of(srv);
    if (s == HP_SOCK_BAD) return hp_of_bool(false);
    /* block up to 50 ms at a time so ctrl-c / ui ticks still get through */
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        hp_sock c = accept(s, (struct sockaddr *)&ss, &sl);
        if (c != HP_SOCK_BAD) {
            sock_nonblocking(c, true);
            sockrec((intptr_t)c, true);
            return hp_of_int((int64_t)(intptr_t)c);
        }
        if (!hp_sock_wouldblock()) return hp_of_bool(false);
        sock_sleep_ms(5);
    }
}

/* stream_socket_accept2(srv, timeoutMs): poll-then-accept, PHP-style.
 * Returns the client handle, or false when nothing arrived within the
 * timeout. Never blocks longer than timeoutMs (in 5 ms slices). */
hval hpbi_stream_socket_accept2(hval srv, hval timeout_ms) {
    hp_net_init();
    hp_sock s = sock_of(srv);
    if (s == HP_SOCK_BAD) return hp_of_bool(false);
    int64_t waited = 0;
    int64_t limit = hp_val_to_int(timeout_ms);
    if (limit < 0) limit = 0;
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        hp_sock c = accept(s, (struct sockaddr *)&ss, &sl);
        if (c != HP_SOCK_BAD) {
            sock_nonblocking(c, true);
            sockrec((intptr_t)c, true);
            return hp_of_int((int64_t)(intptr_t)c);
        }
        if (!hp_sock_wouldblock()) return hp_of_bool(false);
        if (waited >= limit) return hp_of_bool(false);   /* poll timed out */
        sock_sleep_ms(5);
        waited += 5;
    }
}

hval hpbi_stream_set_blocking(hval s, hval flag) {
    hp_sock fd = sock_of(s);
    if (fd == HP_SOCK_BAD) return hp_of_bool(false);
    sock_nonblocking(fd, !hp_val_to_bool(flag));
    return hp_of_bool(true);
}

hval hpbi_stream_recv(hval s, hval maxn) {
    hp_net_init();
    hp_sock fd = sock_of(s);
    if (fd == HP_SOCK_BAD) return hp_of_str(hp_str_lit(""));
    int64_t want = hp_val_to_int(maxn);
    if (want <= 0) want = 4096;
    if (want > (1 << 20)) want = (1 << 20);
    char *buf = (char *)malloc((size_t)want);
    if (!buf) return hp_of_str(hp_str_lit(""));
    int r = recv(fd, buf, (int)want, 0);
    bool blocked = (r < 0) && hp_sock_wouldblock();
    if (r <= 0 && !blocked)
        sockrec((intptr_t)fd, true)->eof = true;
    if (r > 0) {
        hstr *out = hp_str_new(buf, (size_t)r);
        free(buf);
        return hp_of_str(out);
    }
    free(buf);
    if (r == 0) {                    /* orderly shutdown by the peer */
        sockrec((intptr_t)fd, true)->eof = true;
        return hp_of_str(hp_str_lit(""));
    }
    if (!blocked)                    /* hard error: treat as closed */
        sockrec((intptr_t)fd, true)->eof = true;
    return hp_of_str(hp_str_lit(""));
}

hval hpbi_stream_send(hval s, hval data) {
    hp_net_init();
    hp_sock fd = sock_of(s);
    if (fd == HP_SOCK_BAD) return hp_of_int(0);
    hstr *d = hp_val_to_str(data);
    size_t sent = 0;
    int waited_ms = 0;
    while (sent < d->len) {
        int r = send(fd, d->data + sent, (int)(d->len - sent), 0);
        if (r > 0) { sent += (size_t)r; continue; }
        if (r < 0 && hp_sock_wouldblock() && waited_ms < 2000) {
            sock_sleep_ms(1);        /* let the peer drain; frames stay whole */
            waited_ms++;
            continue;
        }
        if (r < 0) sockrec((intptr_t)fd, true)->eof = true;
        break;
    }
    return hp_of_int((int64_t)sent);
}

hval hpbi_stream_ready(hval handles, hval timeout_ms) {
    hp_net_init();
    harr *out = hp_arr_new();
    if (handles.tag != HV_ARR || !handles.u.a) return hp_of_arr(out);
    int64_t n = hp_arr_len(handles.u.a);
    if (n <= 0) return hp_of_arr(out);
#ifdef _WIN32
    WSAPOLLFD *fds = (WSAPOLLFD *)calloc((size_t)n, sizeof(WSAPOLLFD));
#else
    struct pollfd *fds = (struct pollfd *)calloc((size_t)n, sizeof(struct pollfd));
#endif
    if (!fds) return hp_of_arr(out);
    int64_t nvalid = 0;
    for (int64_t i = 0; i < n; i++) {
        hval item = hp_arr_get(handles.u.a, hp_of_int(i));
        hp_sock fd = sock_of(item);
        if (fd == HP_SOCK_BAD) continue;
        fds[nvalid].fd = fd;
        fds[nvalid].events = POLLIN;
        nvalid++;
    }
    int to = (int)hp_val_to_int(timeout_ms);
    if (to < 0) to = 0;
    int r = 0;
    if (nvalid > 0) {
#ifdef _WIN32
        r = WSAPoll(fds, (ULONG)nvalid, to);
#else
        r = poll(fds, (nfds_t)nvalid, to);
#endif
    }
    if (r > 0) {
        int64_t k = 0;
        for (int64_t i = 0; i < n; i++) {
            hval item = hp_arr_get(handles.u.a, hp_of_int(i));
            hp_sock fd = sock_of(item);
            if (fd == HP_SOCK_BAD) continue;
            short rev = (short)fds[k++].revents;
            if (!rev) continue;
            if (rev & (POLLERR | POLLHUP | POLLNVAL))
                sockrec((intptr_t)fd, true)->eof = true;
            if (rev & (POLLIN | POLLERR | POLLHUP))
                hp_arr_push(out, item);
        }
    }
    free(fds);
    return hp_of_arr(out);
}

hval hpbi_stream_eof(hval s) {
    hp_sockrec *rec = sockrec((intptr_t)sock_of(s), false);
    return hp_of_bool(rec ? rec->eof : true);
}

hval hpbi_stream_peer(hval s) {
    hp_sock fd = sock_of(s);
    struct sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    if (fd == HP_SOCK_BAD || getpeername(fd, (struct sockaddr *)&ss, &sl) != 0)
        return hp_of_str(hp_str_lit(""));
    char host[128] = "", port[16] = "";
    if (getnameinfo((struct sockaddr *)&ss, sl, host, sizeof host, port, sizeof port,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        return hp_of_str(hp_str_lit(""));
    hstr *a = hp_str_concat2(hp_str_lit(host), hp_str_lit(":"));
    return hp_of_str(hp_str_concat2(a, hp_str_lit(port)));
}

hval hpbi_stream_close(hval s) {
    hp_sock fd = sock_of(s);
    if (fd == HP_SOCK_BAD) return hp_of_bool(false);
    hp_sock_close(fd);
    sock_forget((intptr_t)fd);
    return hp_of_bool(true);
}

/* PHP-style callable dispatch: any hval may hold a closure. */
hval hp_call_value(hval fn, int nargs, const hval *args) {
    if (fn.tag == HV_CLO && fn.u.c) return hp_closure_call_arr(fn.u.c, (hval *)args, nargs);
    hstr *msg = hp_str_concat2(hp_str_lit("value of type "),
                               hp_str_lit(hp_kind_name(fn.tag)));
    msg = hp_str_concat2(msg, hp_str_lit(" is not callable"));
    hp_throw(hp_of_str(msg));
    return hp_null;
}

/* ---------- UI builtins (thin wrappers over hphp_ui.c) ---------- */
static hval uistr(const char *s) { return hp_of_str(hp_str_lit(s ? s : "")); }

hval hpbi_ui_dispatch(hval ms) { return hp_of_int(hpui_dispatch((int)hp_val_to_int(ms))); }
hval hpbi_ui_run_main(void) { hpui_run_main(); return hp_null; }
hval hpbi_ui_quit(void) { hpui_quit(); return hp_null; }

/* ui_window(title, x, y, w, h) — x,y = -32000 centers the window */
hval hpbi_ui_window(hval title, hval x, hval y, hval w, hval hgt) {
    hstr *t = hp_val_to_str(title);
    return hp_of_int(hpui_window_new(t->data, (int)hp_val_to_int(x),
                                     (int)hp_val_to_int(y), (int)hp_val_to_int(w),
                                     (int)hp_val_to_int(hgt)));
}

hval hpbi_ui_show(hval h, hval vis) {
    return hp_of_int(hpui_window_show((int64_t)hp_val_to_int(h), hp_val_to_bool(vis)));
}
hval hpbi_ui_title(hval h, hval t) {
    hstr *s = hp_val_to_str(t);
    return hp_of_int(hpui_window_title((int64_t)hp_val_to_int(h), s->data));
}
hval hpbi_ui_size(hval h, hval w, hval hgt) {
    return hp_of_int(hpui_window_size((int64_t)hp_val_to_int(h),
                                      (int)hp_val_to_int(w), (int)hp_val_to_int(hgt)));
}
hval hpbi_ui_pos(hval h, hval x, hval y) {
    return hp_of_int(hpui_window_pos((int64_t)hp_val_to_int(h),
                                     (int)hp_val_to_int(x), (int)hp_val_to_int(y)));
}
hval hpbi_ui_close(hval h) {
    return hp_of_int(hpui_window_close((int64_t)hp_val_to_int(h)));
}
hval hpbi_ui_alive(hval h) {
    return hp_of_bool(hpui_window_alive((int64_t)hp_val_to_int(h)));
}

/* ui_add(parent, kind, text, x, y, w, h) — kind uses the HPUI_* numbers */
hval hpbi_ui_add(hval parent, hval kind, hval text, hval x, hval y,
                hval w, hval hgt) {
    hstr *t = hp_val_to_str(text);
    return hp_of_int(hpui_ctrl_new((int64_t)hp_val_to_int(kind),
                                   (int64_t)hp_val_to_int(parent), t->data,
                                   (int)hp_val_to_int(x), (int)hp_val_to_int(y),
                                   (int)hp_val_to_int(w), (int)hp_val_to_int(hgt)));
}
hval hpbi_ui_set(hval h, hval text) {
    hstr *s = hp_val_to_str(text);
    return hp_of_int(hpui_ctrl_set_text((int64_t)hp_val_to_int(h), s->data));
}
hval hpbi_ui_get(hval h) {
    hstr *out = NULL;
    if (hpui_ctrl_text((int64_t)hp_val_to_int(h), &out) != 0 || !out)
        return hp_of_str(hp_str_lit(""));
    return hp_of_str(out);
}
hval hpbi_ui_enable(hval h, hval on) {
    return hp_of_int(hpui_ctrl_enable((int64_t)hp_val_to_int(h), hp_val_to_bool(on)));
}
hval hpbi_ui_move(hval h, hval x, hval y, hval w, hval hgt) {
    return hp_of_int(hpui_ctrl_move((int64_t)hp_val_to_int(h), (int)hp_val_to_int(x),
                                    (int)hp_val_to_int(y), (int)hp_val_to_int(w),
                                    (int)hp_val_to_int(hgt)));
}
hval hpbi_ui_focus(hval h) {
    return hp_of_int(hpui_ctrl_focus((int64_t)hp_val_to_int(h)));
}
hval hpbi_ui_items(hval h, hval item) {
    hstr *s = hp_val_to_str(item);
    return hp_of_int(hpui_list_add((int64_t)hp_val_to_int(h), s->data));
}
hval hpbi_ui_clear(hval h) {
    return hp_of_int(hpui_list_clear((int64_t)hp_val_to_int(h)));
}
hval hpbi_ui_remove(hval h, hval idx) {
    return hp_of_int(hpui_list_remove((int64_t)hp_val_to_int(h),
                                      (int64_t)hp_val_to_int(idx)));
}
hval hpbi_ui_ctrl_show(hval h, hval vis) {
    return hp_of_int(hpui_ctrl_show((int64_t)hp_val_to_int(h), hp_val_to_bool(vis)));
}
hval hpbi_ui_selected(hval h) {
    int64_t sel = -1;
    hpui_list_selected((int64_t)hp_val_to_int(h), &sel);
    return hp_of_int(sel);
}
hval hpbi_ui_select(hval h, hval idx) {
    return hp_of_int(hpui_list_select((int64_t)hp_val_to_int(h),
                                      (int64_t)hp_val_to_int(idx)));
}
hval hpbi_ui_item_text(hval h, hval idx) {
    hstr *out = NULL;
    if (hpui_list_text((int64_t)hp_val_to_int(h), (int64_t)hp_val_to_int(idx),
                       &out) != 0 || !out)
        return hp_of_str(hp_str_lit(""));
    return hp_of_str(out);
}
hval hpbi_ui_check_get(hval h) {
    bool on = false;
    hpui_check_get((int64_t)hp_val_to_int(h), &on);
    return hp_of_int(on ? 1 : 0);
}
hval hpbi_ui_check_set(hval h, hval on) {
    return hp_of_int(hpui_check_set((int64_t)hp_val_to_int(h), hp_val_to_bool(on)));
}
hval hpbi_ui_progress(hval h, hval pct) {
    return hp_of_int(hpui_progress_set((int64_t)hp_val_to_int(h),
                                       (int64_t)hp_val_to_int(pct)));
}
hval hpbi_ui_slider_get(hval h) {
    int64_t pos = 0;
    hpui_slider_get((int64_t)hp_val_to_int(h), &pos);
    return hp_of_int(pos);
}
hval hpbi_ui_slider_set(hval h, hval pos) {
    return hp_of_int(hpui_slider_set((int64_t)hp_val_to_int(h),
                                     (int64_t)hp_val_to_int(pos)));
}
hval hpbi_ui_bg(hval h, hval rgb) {
    return hp_of_int(hpui_ctrl_set_bg((int64_t)hp_val_to_int(h),
                                      (int64_t)hp_val_to_int(rgb)));
}
hval hpbi_ui_fg(hval h, hval rgb) {
    return hp_of_int(hpui_ctrl_set_fg((int64_t)hp_val_to_int(h),
                                      (int64_t)hp_val_to_int(rgb)));
}
hval hpbi_ui_font(hval h, hval size, hval bold) {
    return hp_of_int(hpui_ctrl_font((int64_t)hp_val_to_int(h),
                                    (int64_t)hp_val_to_int(size),
                                    hp_val_to_bool(bold)));
}
hval hpbi_ui_menu(hval win, hval label) {
    hstr *s = hp_val_to_str(label);
    return hp_of_int(hpui_menu_new((int64_t)hp_val_to_int(win), s->data));
}
hval hpbi_ui_menu_item(hval menu, hval label, hval cmdid) {
    hstr *s = hp_val_to_str(label);
    return hp_of_int(hpui_menu_item((int64_t)hp_val_to_int(menu), s->data,
                                    (int64_t)hp_val_to_int(cmdid)));
}
hval hpbi_ui_menu_sep(hval menu) {
    return hp_of_int(hpui_menu_sep((int64_t)hp_val_to_int(menu)));
}
hval hpbi_ui_menu_check(hval h) {
    return hp_of_int(hpui_ctrl_checked((int64_t)hp_val_to_int(h)));
}

/* ui_on(handle, event, callback) — event: 1 click, 2 change, 3 close, 4 key */
hval hpbi_ui_on(hval h, hval ev, hval cb) {
    return hp_of_int(hpui_on_event((int64_t)hp_val_to_int(h),
                                   (int64_t)hp_val_to_int(ev), cb));
}
hval hpbi_ui_timer(hval ms, hval cb) {
    return hp_of_int(hpui_timer((int64_t)hp_val_to_int(ms), cb));
}
hval hpbi_ui_msg(hval win, hval title, hval text, hval type) {
    hstr *t = hp_val_to_str(title), *x = hp_val_to_str(text);
    return hp_of_int(hpui_msgbox((int64_t)hp_val_to_int(win), t->data, x->data,
                                 (int)hp_val_to_int(type)));
}
hval hpbi_ui_open_file(hval win, hval filter) {
    hstr *f = hp_val_to_str(filter);
    return hpui_open_file((int64_t)hp_val_to_int(win), f->data);
}
hval hpbi_ui_save_file(hval win, hval filter) {
    hstr *f = hp_val_to_str(filter);
    return hpui_save_file((int64_t)hp_val_to_int(win), f->data);
}
hval hpbi_ui_pick_folder(hval win) {
    return hpui_pick_folder((int64_t)hp_val_to_int(win));
}
hval hpbi_ui_pick_color(hval win, hval init) {
    return hpui_color_pick((int64_t)hp_val_to_int(win), (int64_t)hp_val_to_int(init));
}
hval hpbi_ui_clip_set(hval text) {
    hstr *s = hp_val_to_str(text);
    return hp_of_int(ui_clip_set(s->data));
}
hval hpbi_ui_clip_get(void) { return ui_clip_get(); }
