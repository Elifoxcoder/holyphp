/* ==== HolyPHP MaxOut engine - twelve-dollar-server-challenge SPEC.md ====
 *
 * The same io_uring skeleton as the MaxOut engine, speaking the challenge API:
 *
 *   GET  /health              {"status":"ok","db":"ok","uptime_s":<n>}
 *   GET  /feed                {"posts":[<post> x 20]}   newest first
 *   GET  /posts/:id           {"post":<post>}
 *   POST /posts          auth 201  {"post":<post with like_count 0>}
 *   POST /posts/:id/like  auth 201 first time, 200 on repeats
 *
 * where <post> is {"id":..,"body":..,"created_at":..,"author":..,"like_count":..}
 * exactly in that order - the golden tests compare bytes.
 *
 * Engine properties:
 *   - multi-shot accept, single-shot recv into the connection's own buffer
 *     (measured faster than multishot + provided buffers here; HPHP_MR=1 opts in)
 *   - DEFER_TASKRUN | SINGLE_ISSUER, 4096 ring entries, SQ-full recovery
 *   - one fixed 16 KiB record per connection: request in, response out, no
 *     per-request heap allocation; idle connections keep no buffers
 *   - prepared statements, WAL + synchronous=NORMAL, mmap'd pager
 *   - group commit: every write harvested in one CQE batch shares one
 *     BEGIN IMMEDIATE .. COMMIT, and a response is queued only after the
 *     commit succeeded
 *   - JWT HS256 verified in place (base64url + HMAC-SHA256 written from
 *     scratch: no OpenSSL, no allocation)
 *   - JSON rendered straight from SQLite column bytes into the response
 *     buffer; the request body string is decoded once on the way in and
 *     escaped once on the way out
 *
 * Reads are deliberately live - no feed cache - because like_count has to be
 * correct as of this request (the test likes a post and immediately re-reads).
 *
 *   hpbi_holy_challenge_start(port, db_path)   JWT_SECRET from the environment
 * ===================================================================== */
#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <liburing.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

#define C_MAX_SLOTS   2048
#define C_RECV_SIZE   4096
#define C_REQ_SIZE    4096
#define C_RESP_SIZE   8192
#define C_RING        4096
#define C_BATCH       4096
#define C_FEED_LIMIT  20
#define C_USER_MAX    64
#define C_BODY_MAX    512
#define C_NBUF        1024
#define C_BGID        2

enum { C_FREE = 0, C_READING = 1, C_WRITING = 2, C_QUEUED = 3 };
enum { C_ACCEPT = 0, C_RECV = 1, C_SEND = 2 };
#define CTAG(idx, op) (((uint64_t)(idx) << 2) | (uint64_t)(op))

typedef struct {
    int fd;
    int state;
    int keep_alive;
    uint32_t gen;
    size_t req_len;
    size_t resp_len;
    size_t resp_off;
    const char *resp_ptr;
    char recv_buf[C_RECV_SIZE];
    char req_buf[C_REQ_SIZE];
    char resp_buf[C_RESP_SIZE];
} CSlot;

enum { W_CREATE = 1, W_LIKE = 2 };

typedef struct {
    int slot_idx;
    uint32_t gen;
    int kind;
    int64_t user_id;
    int64_t post_id;
    char username[C_USER_MAX];
    char body[C_BODY_MAX + 1];
    int body_len;
} CPending;

static int c_multishot = 0;   /* HPHP_MR=1 -> multishot recv + provided buffers */

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *st_health, *st_feed, *st_post;
    sqlite3_stmt *st_insert, *st_like_exists, *st_like_ins;
    sqlite3_stmt *st_begin, *st_commit;
    CSlot *slots;
    int *free_slots;
    int free_count;
    CPending *pending;
    int pending_count;
    int64_t start_time;
    const char *secret;
    size_t secret_len;
    size_t cq_harvested;
    struct io_uring *ring;
    struct io_uring_buf_ring *br;
    char *buf_base;
} CEngine;

#define APPEND(p, str) do { memcpy((p), (str), sizeof(str) - 1); (p) += sizeof(str) - 1; } while (0)

/* ------------------------------------------------------------------ numbers */
static inline char *c_u64toa(char *p, uint64_t v) {
    if (v == 0) { *p++ = '0'; return p; }
    char t[24]; int i = 0;
    while (v > 0) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) *p++ = t[--i];
    return p;
}
static inline char *c_i64toa(char *p, int64_t v) {
    if (v < 0) { *p++ = '-'; v = -v; }
    return c_u64toa(p, (uint64_t)v);
}

/* ------------------------------------------------------------------- search */
static inline void *c_mem_find(const void *hay, size_t hay_len, const void *nd, size_t nd_len) {
    if (nd_len == 0 || hay_len < nd_len) return NULL;
    const char *h = (const char *)hay;
    const char first = ((const char *)nd)[0];
    const char *end = h + hay_len - nd_len;
    const char *p = h;
    while (p <= end) {
        p = (const char *)memchr(p, first, (size_t)(end - p) + 1);
        if (!p) return NULL;
        if (memcmp(p, nd, nd_len) == 0) return (void *)p;
        p++;
    }
    return NULL;
}
static inline int c_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static void *mem_find_ci(const void *hay, size_t hay_len, const char *nd, size_t nd_len) {
    if (nd_len == 0 || hay_len < nd_len) return NULL;
    const char *h = (const char *)hay;
    const char *end = h + hay_len - nd_len;
    for (const char *p = h; p <= end; p++) {
        size_t i = 0;
        while (i < nd_len && c_lower((unsigned char)p[i]) == c_lower((unsigned char)nd[i])) i++;
        if (i == nd_len) return (void *)p;
    }
    return NULL;
}

/* ------------------------------------------------------------- sha256/hmac */
typedef struct {
    uint32_t st[8];
    uint64_t bits;
    unsigned char buf[64];
    uint32_t len;
} CSha;

static const uint32_t C_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void c_sha_transform(CSha *c, const unsigned char *d) {
    uint32_t w[64], a, b, cc, dd, e, f, g, h, t1, t2, s0, s1;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)d[i*4] << 24) | ((uint32_t)d[i*4+1] << 16) |
               ((uint32_t)d[i*4+2] << 8) | (uint32_t)d[i*4+3];
    for (int i = 16; i < 64; i++) {
        s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->st[0]; b = c->st[1]; cc = c->st[2]; dd = c->st[3];
    e = c->st[4]; f = c->st[5]; g = c->st[6]; h = c->st[7];
    for (int i = 0; i < 64; i++) {
        s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        t1 = h + s1 + ((e & f) ^ (~e & g)) + C_K[i] + w[i];
        s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        t2 = s0 + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = dd + t1; dd = cc; cc = b; b = a; a = t1 + t2;
    }
    c->st[0] += a; c->st[1] += b; c->st[2] += cc; c->st[3] += dd;
    c->st[4] += e; c->st[5] += f; c->st[6] += g; c->st[7] += h;
}
static void c_sha_init(CSha *c) {
    c->st[0]=0x6a09e667u; c->st[1]=0xbb67ae85u; c->st[2]=0x3c6ef372u; c->st[3]=0xa54ff53au;
    c->st[4]=0x510e527fu; c->st[5]=0x9b05688cu; c->st[6]=0x1f83d9abu; c->st[7]=0x5be0cd19u;
    c->bits = 0; c->len = 0;
}
static void c_sha_update(CSha *c, const unsigned char *d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        c->buf[c->len++] = d[i];
        if (c->len == 64) { c_sha_transform(c, c->buf); c->bits += 512; c->len = 0; }
    }
}
static void c_sha_final(CSha *c, unsigned char out[32]) {
    uint64_t bits = c->bits + (uint64_t)c->len * 8;
    c->buf[c->len++] = 0x80;
    if (c->len > 56) {
        while (c->len < 64) c->buf[c->len++] = 0;
        c_sha_transform(c, c->buf); c->len = 0;
    }
    while (c->len < 56) c->buf[c->len++] = 0;
    for (int i = 7; i >= 0; i--) c->buf[c->len++] = (unsigned char)(bits >> (i * 8));
    c_sha_transform(c, c->buf);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(c->st[i] >> 24);
        out[i*4+1] = (unsigned char)(c->st[i] >> 16);
        out[i*4+2] = (unsigned char)(c->st[i] >> 8);
        out[i*4+3] = (unsigned char)(c->st[i]);
    }
}
static void c_hmac_sha256(const char *key, size_t key_len,
                          const unsigned char *msg, size_t msg_len, unsigned char out[32]) {
    unsigned char k[64], inner[32], pad[64];
    memset(k, 0, sizeof k);
    if (key_len > 64) {
        CSha t; c_sha_init(&t); c_sha_update(&t, (const unsigned char *)key, key_len); c_sha_final(&t, k);
    } else {
        memcpy(k, key, key_len);
    }
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    CSha c; c_sha_init(&c); c_sha_update(&c, pad, 64); c_sha_update(&c, msg, msg_len); c_sha_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    c_sha_init(&c); c_sha_update(&c, pad, 64); c_sha_update(&c, inner, 32); c_sha_final(&c, out);
}

/* --------------------------------------------------------- base64url codec */
static int c_b64url_val(int ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '-') return 62;
    if (ch == '_') return 63;
    return -1;
}
static int c_b64url_dec(const char *in, size_t n, unsigned char *out, size_t cap, size_t *out_len) {
    size_t o = 0; uint32_t acc = 0; int bits = 0;
    if (n % 4 == 1) return -1;
    for (size_t i = 0; i < n; i++) {
        int v = c_b64url_val((unsigned char)in[i]);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    *out_len = o;
    return 0;
}
static void c_b64url_enc(const unsigned char *in, size_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i+1] << 8;
        if (i + 2 < n) v |= (uint32_t)in[i+2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = T[(v >> 6) & 63];
        if (i + 2 < n) out[o++] = T[v & 63];
    }
    out[o] = 0;
}

/* ------------------------------------------------------------- json helpers */
static const char *c_skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}
/* skip one string literal starting at p (must be '"'), return past the quote */
static const char *c_skip_str(const char *p, const char *end) {
    if (p >= end || *p != '"') return NULL;
    p++;
    while (p < end) {
        if (*p == '"') return p + 1;
        if (*p == '\\') { p++; if (p >= end) return NULL; }
        p++;
    }
    return NULL;
}
/* skip one value; returns past it or NULL */
static const char *c_skip_value(const char *p, const char *end, int depth) {
    if (depth > 32 || p >= end) return NULL;
    p = c_skip_ws(p, end);
    if (p >= end) return NULL;
    if (*p == '"') return c_skip_str(p, end);
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open == '{') ? '}' : ']';
        p++;
        p = c_skip_ws(p, end);
        if (p < end && *p == close) return p + 1;
        for (;;) {
            if (open == '{') {
                p = c_skip_ws(p, end);
                p = c_skip_str(p, end);
                if (!p) return NULL;
                p = c_skip_ws(p, end);
                if (p >= end || *p != ':') return NULL;
                p++;
            }
            p = c_skip_value(p, end, depth + 1);
            if (!p) return NULL;
            p = c_skip_ws(p, end);
            if (p >= end) return NULL;
            if (*p == ',') { p++; continue; }
            if (*p == close) return p + 1;
            return NULL;
        }
    }
    /* number / true / false / null */
    const char *s = p;
    while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' ||
                       (*p >= '0' && *p <= '9') ||
                       *p == 't' || *p == 'r' || *p == 'u' || *p == 'e' ||
                       *p == 'f' || *p == 'a' || *p == 'l' || *p == 's')) p++;
    if (p == s) return NULL;
    return p;
}
/* whole document valid? */
static int c_json_valid(const char *p, const char *end) {
    p = c_skip_value(p, end, 0);
    if (!p) return 0;
    p = c_skip_ws(p, end);
    return p == end;
}
/* top-level member of an object: returns pointer to the value, sets kind
   ('s' = string, other), or NULL when the key is absent. */
static const char *c_json_get(const char *p, const char *end, const char *key, size_t klen, char *kind) {
    p = c_skip_ws(p, end);
    if (p >= end || *p != '{') return NULL;
    p++;
    p = c_skip_ws(p, end);
    if (p < end && *p == '}') return NULL;
    for (;;) {
        p = c_skip_ws(p, end);
        if (p >= end || *p != '"') return NULL;
        const char *kstart = p + 1;
        const char *kend = c_skip_str(p, end);
        if (!kend) return NULL;
        size_t kn = (size_t)(kend - 1 - kstart);
        const char *after = c_skip_ws(kend, end);
        if (after >= end || *after != ':') return NULL;
        const char *val = c_skip_ws(after + 1, end);
        if (val >= end) return NULL;
        int match = (kn == klen && memcmp(kstart, key, klen) == 0);
        if (match) {
            *kind = (*val == '"') ? 's' : 'o';
            return val;
        }
        const char *next = c_skip_value(val, end, 1);
        if (!next) return NULL;
        next = c_skip_ws(next, end);
        if (next >= end) return NULL;
        if (*next == ',') { p = next + 1; continue; }
        if (*next == '}') return NULL;
        return NULL;
    }
}
/* decode a JSON string literal at p into dst; returns past the quote or NULL */
static const char *c_json_dec_str(const char *p, const char *end, char *dst, size_t cap, size_t *out_len) {
    if (p >= end || *p != '"') return NULL;
    p++;
    size_t n = 0;
    while (p < end && *p != '"') {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\\') {
            p++;
            if (p >= end) return NULL;
            char e = *p++;
            if (e == 'u') {
                if (end - p < 4) return NULL;
                uint32_t cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = p[i]; int v;
                    if (h >= '0' && h <= '9') v = h - '0';
                    else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                    else return NULL;
                    cp = (cp << 4) | (uint32_t)v;
                }
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    uint32_t lo = 0; int ok = 1;
                    for (int i = 0; i < 4; i++) {
                        char h = p[2+i]; int v;
                        if (h >= '0' && h <= '9') v = h - '0';
                        else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                        else { ok = 0; break; }
                        lo = (lo << 4) | (uint32_t)v;
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                if (cp < 0x80) {
                    if (n + 1 >= cap) return NULL;
                    dst[n++] = (char)cp;
                } else if (cp < 0x800) {
                    if (n + 2 >= cap) return NULL;
                    dst[n++] = (char)(0xC0 | (cp >> 6));
                    dst[n++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    if (n + 3 >= cap) return NULL;
                    dst[n++] = (char)(0xE0 | (cp >> 12));
                    dst[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    dst[n++] = (char)(0x80 | (cp & 0x3F));
                }
                continue;
            }
            switch (e) {
                case '"':  ch = '"';  break;
                case '\\': ch = '\\'; break;
                case '/':  ch = '/';  break;
                case 'b':  ch = '\b'; break;
                case 'f':  ch = '\f'; break;
                case 'n':  ch = '\n'; break;
                case 'r':  ch = '\r'; break;
                case 't':  ch = '\t'; break;
                default: return NULL;
            }
            /* p already sits past the escaped character */
            if (n + 1 >= cap) return NULL;
            dst[n++] = (char)ch;
            continue;
        }
        if (n + 1 >= cap) return NULL;
        dst[n++] = (char)ch;
        p++;
    }
    if (p >= end || *p != '"') return NULL;
    dst[n] = 0;
    if (out_len) *out_len = n;
    return p + 1;
}
static char *c_json_str(char *p, const char *s, size_t n) {
    *p++ = '"';
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')       { *p++ = '\\'; *p++ = '"'; }
        else if (c == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else if (c == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (c == '\r') { *p++ = '\\'; *p++ = 'r'; }
        else if (c == '\t') { *p++ = '\\'; *p++ = 't'; }
        else if (c < 0x20) {
            static const char H[] = "0123456789abcdef";
            *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
            *p++ = H[(c >> 4) & 15]; *p++ = H[c & 15];
        } else *p++ = (char)c;
    }
    *p++ = '"';
    return p;
}
static const char *c_trim(const char *s, size_t *n) {
    size_t a = 0, b = *n;
    while (a < b && (s[a]==' '||s[a]=='\t'||s[a]=='\n'||s[a]=='\r'||s[a]=='\f'||s[a]=='\v')) a++;
    while (b > a && (s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\n'||s[b-1]=='\r'||s[b-1]=='\f'||s[b-1]=='\v')) b--;
    *n = b - a;
    return s + a;
}

/* ---------------------------------------------------------------- responses */
static void c_respond(CSlot *s, int code, const char *reason,
                      const char *ctype, const char *body, size_t blen) {
    char *p = s->resp_buf;
    APPEND(p, "HTTP/1.1 ");
    p = c_u64toa(p, (uint64_t)code);
    *p++ = ' ';
    memcpy(p, reason, strlen(reason)); p += strlen(reason);
    APPEND(p, "\r\nContent-Type: ");
    memcpy(p, ctype, strlen(ctype)); p += strlen(ctype);
    APPEND(p, "\r\nContent-Length: ");
    p = c_u64toa(p, blen);
    if (s->keep_alive) APPEND(p, "\r\nConnection: keep-alive\r\n\r\n");
    else               APPEND(p, "\r\nConnection: close\r\n\r\n");
    memcpy(p, body, blen); p += blen;
    s->resp_ptr = s->resp_buf;
    s->resp_off = 0;
    s->resp_len = (size_t)(p - s->resp_buf);
    s->state = C_WRITING;
}
static void c_error(CSlot *s, int code, const char *reason, const char *body) {
    c_respond(s, code, reason, "application/json", body, strlen(body));
}
static void c_not_found_route(CSlot *s) {
    c_respond(s, 404, "Not Found", "application/json", "{\"error\":\"not found\"}", 22);
}

/* ------------------------------------------------------------------- auth */
enum { AUTH_OK = 0, AUTH_MISSING = 1, AUTH_BAD = 2, AUTH_PAYLOAD = 3 };

static int c_check_auth(CEngine *e, const char *req, size_t hdr_len,
                        int64_t *user_id, char *username, size_t *ulen) {
    const char *h = (const char *)mem_find_ci(req, hdr_len, "authorization:", 14);
    if (!h) return AUTH_MISSING;
    const char *end = req + hdr_len;
    const char *v = h + 14;
    while (v < end && (*v == ' ' || *v == '\t')) v++;
    if ((size_t)(end - v) < 7) return AUTH_MISSING;
    if (c_lower((unsigned char)v[0])!='b' || c_lower((unsigned char)v[1])!='e' ||
        c_lower((unsigned char)v[2])!='a' || c_lower((unsigned char)v[3])!='r' ||
        c_lower((unsigned char)v[4])!='e' || c_lower((unsigned char)v[5])!='r' ||
        v[6] != ' ') return AUTH_MISSING;
    v += 7;
    const char *t_end = (const char *)memchr(v, '\r', (size_t)(end - v));
    if (!t_end) t_end = end;
    while (t_end > v && (t_end[-1] == ' ' || t_end[-1] == '\t')) t_end--;
    size_t tlen = (size_t)(t_end - v);
    if (tlen == 0) return AUTH_MISSING;

    /* header.payload.signature */
    const char *d1 = (const char *)memchr(v, '.', tlen);
    if (!d1) return AUTH_BAD;
    const char *d2 = (const char *)memchr(d1 + 1, '.', (size_t)(t_end - d1 - 1));
    if (!d2 || d2 + 1 >= t_end) return AUTH_BAD;

    /* alg must be HS256 */
    unsigned char hbuf[128]; size_t hlen = 0;
    if (c_b64url_dec(v, (size_t)(d1 - v), hbuf, sizeof hbuf, &hlen) != 0) return AUTH_BAD;
    hbuf[hlen] = 0;
    char kind = 0;
    const char *alg = c_json_get((const char *)hbuf, (const char *)hbuf + hlen, "alg", 3, &kind);
    if (!alg || kind != 's') return AUTH_BAD;
    if ((size_t)((const char *)hbuf + hlen - alg) < 7 || memcmp(alg, "\"HS256\"", 7) != 0)
        return AUTH_BAD;

    /* signature over "header.payload" */
    unsigned char mac[32]; char enc[48];
    c_hmac_sha256(e->secret, e->secret_len, (const unsigned char *)v,
                  (size_t)(d2 - v), mac);
    c_b64url_enc(mac, 32, enc);
    size_t sig_len = (size_t)(t_end - d2 - 1);
    if (sig_len != 43 || memcmp(d2 + 1, enc, 43) != 0) return AUTH_BAD;

    /* payload */
    unsigned char pbuf[768]; size_t plen = 0;
    if (c_b64url_dec(d1 + 1, (size_t)(d2 - d1 - 1), pbuf, sizeof pbuf - 1, &plen) != 0)
        return AUTH_BAD;
    pbuf[plen] = 0;
    const char *ps = (const char *)pbuf, *pe = ps + plen;
    if (!c_json_valid(ps, pe)) return AUTH_BAD;

    /* exp (optional) */
    kind = 0;
    const char *exp = c_json_get(ps, pe, "exp", 3, &kind);
    if (exp && kind != 's') {
        int64_t v64 = 0; int neg = 0; const char *q = exp;
        if (*q == '-') { neg = 1; q++; }
        while (q < pe && *q >= '0' && *q <= '9') { v64 = v64 * 10 + (*q - '0'); q++; }
        if (neg) v64 = -v64;
        if (v64 != 0 && v64 <= (int64_t)time(NULL)) return AUTH_BAD;
    }

    /* sub: positive integer string */
    kind = 0;
    const char *sub = c_json_get(ps, pe, "sub", 3, &kind);
    if (!sub || kind != 's') return AUTH_PAYLOAD;
    const char *se = (const char *)c_skip_str(sub, pe);
    if (!se) return AUTH_PAYLOAD;
    const char *q = sub + 1;
    size_t sl = (size_t)(se - 1 - q);
    if (sl == 0 || sl > 18) return AUTH_PAYLOAD;
    int64_t id = 0;
    for (size_t i = 0; i < sl; i++) {
        if (q[i] < '0' || q[i] > '9') return AUTH_PAYLOAD;
        id = id * 10 + (q[i] - '0');
    }
    if (id <= 0) return AUTH_PAYLOAD;

    /* username: string */
    kind = 0;
    const char *un = c_json_get(ps, pe, "username", 8, &kind);
    if (!un || kind != 's') return AUTH_PAYLOAD;
    size_t n = 0;
    if (!c_json_dec_str(un, pe, username, C_USER_MAX, &n)) return AUTH_PAYLOAD;
    if (n == 0) return AUTH_PAYLOAD;
    *user_id = id;
    *ulen = n;
    return AUTH_OK;
}

/* --------------------------------------------------------------- sql render */
/* columns: 0 id, 1 body, 2 created_at, 3 username, 4 like_count */
static char *c_render_post(sqlite3_stmt *st, char *p) {
    APPEND(p, "{\"id\":");
    p = c_i64toa(p, sqlite3_column_int64(st, 0));

    const unsigned char *b = sqlite3_column_text(st, 1);
    int bl = sqlite3_column_bytes(st, 1);
    APPEND(p, ",\"body\":");
    p = c_json_str(p, (const char *)(b ? b : (const unsigned char *)""), (size_t)(b ? bl : 0));

    const unsigned char *c = sqlite3_column_text(st, 2);
    int cl = sqlite3_column_bytes(st, 2);
    APPEND(p, ",\"created_at\":");
    p = c_json_str(p, (const char *)(c ? c : (const unsigned char *)""), (size_t)(c ? cl : 0));

    const unsigned char *u = sqlite3_column_text(st, 3);
    int ul = sqlite3_column_bytes(st, 3);
    APPEND(p, ",\"author\":");
    p = c_json_str(p, (const char *)(u ? u : (const unsigned char *)""), (size_t)(u ? ul : 0));

    APPEND(p, ",\"like_count\":");
    p = c_i64toa(p, sqlite3_column_int64(st, 4));
    *p++ = '}';
    return p;
}
static char *c_render_new_post(char *p, int64_t id, const char *body, size_t blen,
                               const char *created, size_t clen,
                               const char *author, size_t alen) {
    APPEND(p, "{\"id\":");
    p = c_i64toa(p, id);
    APPEND(p, ",\"body\":");
    p = c_json_str(p, body, blen);
    APPEND(p, ",\"created_at\":");
    p = c_json_str(p, created, clen);
    APPEND(p, ",\"author\":");
    p = c_json_str(p, author, alen);
    APPEND(p, ",\"like_count\":0}");
    return p;
}

/* ------------------------------------------------------------ io_uring bits */
static inline struct io_uring_sqe *c_get_sqe(CEngine *e) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (sqe) return sqe;
    if (e->cq_harvested) {
        io_uring_cq_advance(e->ring, e->cq_harvested);
        e->cq_harvested = 0;
    }
    io_uring_submit(e->ring);
    return io_uring_get_sqe(e->ring);
}
static void c_arm_send(CEngine *e, int idx) {
    CSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = c_get_sqe(e);
    if (!sqe) return;
    io_uring_prep_send(sqe, s->fd, s->resp_ptr + s->resp_off,
                       s->resp_len - s->resp_off, MSG_NOSIGNAL);
    io_uring_sqe_set_data64(sqe, CTAG(idx, C_SEND));
}
static void c_arm_recv(CEngine *e, int idx) {
    CSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = c_get_sqe(e);
    if (!sqe) return;
    if (c_multishot) {
        io_uring_prep_recv_multishot(sqe, s->fd, NULL, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = C_BGID;
    } else {
        io_uring_prep_recv(sqe, s->fd, s->recv_buf, C_RECV_SIZE, 0);
    }
    io_uring_sqe_set_data64(sqe, CTAG(idx, C_RECV));
}
static void c_arm_accept(CEngine *e, int listen_fd) {
    struct io_uring_sqe *sqe = c_get_sqe(e);
    if (!sqe) return;
    io_uring_prep_multishot_accept(sqe, listen_fd, NULL, NULL, 0);
    io_uring_sqe_set_data64(sqe, CTAG(0, C_ACCEPT));
}
static inline void c_return_buf(CEngine *e, unsigned bid) {
    io_uring_buf_ring_add(e->br, e->buf_base + (size_t)bid * C_RECV_SIZE,
                          C_RECV_SIZE, (unsigned short)bid, C_NBUF - 1, 0);
    io_uring_buf_ring_advance(e->br, 1);
}
static inline void c_release(CEngine *e, int idx) {
    CSlot *s = &e->slots[idx];
    if (s->state == C_FREE) return;
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    s->state = C_FREE;
    s->gen++;
    e->free_slots[e->free_count++] = idx;
}

/* ------------------------------------------------------------- request path */
/* 1 = response ready, 2 = write queued, 0 = need more bytes */
static int c_parse(CEngine *e, int idx, const char *req, size_t len) {
    CSlot *s = &e->slots[idx];

    char *hdr_p = (char *)c_mem_find(req, len, "\r\n\r\n", 4);
    if (!hdr_p) return 0;
    size_t hdr_len = (size_t)(hdr_p - req) + 4;
    const char *hend = req + hdr_len;

    s->keep_alive = mem_find_ci(req, hdr_len, "connection: close", 17) == NULL;

    int is_get  = (len >= 4 && memcmp(req, "GET ", 4) == 0);
    int is_post = (len >= 5 && memcmp(req, "POST ", 5) == 0);
    if (!is_get && !is_post) { c_not_found_route(s); return 1; }

    const char *target = req + (is_get ? 4 : 5);
    const char *tend = (const char *)memchr(target, ' ', (size_t)(hend - target));
    if (!tend) { c_not_found_route(s); return 1; }
    size_t tlen = (size_t)(tend - target);
    if (tlen == 0 || target[0] != '/') { c_not_found_route(s); return 1; }
    const char *path = target + 1;
    size_t plen = tlen - 1;

    /* body framing for POST */
    size_t body_len = 0;
    if (is_post) {
        const char *cl = (const char *)mem_find_ci(req, hdr_len, "\r\ncontent-length: ", 18);
        if (cl) {
            const char *q = cl + 18;
            while (q < hend && *q == ' ') q++;
            while (q < hend && *q >= '0' && *q <= '9') { body_len = body_len * 10 + (size_t)(*q - '0'); q++; }
        }
        if (len < hdr_len + body_len) return 0;
    }
    const char *body = req + hdr_len;
    const char *body_end = body + body_len;

    /* ---------------- reads that need no auth ---------------- */
    if (is_get) {
        if (plen == 6 && memcmp(path, "health", 6) == 0) {
            char bb[192]; char *p = bb;
            APPEND(p, "{\"status\":\"");
            if (sqlite3_step(e->st_health) == SQLITE_ROW) {
                sqlite3_reset(e->st_health);
                APPEND(p, "ok\",\"db\":\"ok\",\"uptime_s\":");
                p = c_i64toa(p, (int64_t)(time(NULL) - e->start_time));
                *p++ = '}';
                c_respond(s, 200, "OK", "application/json", bb, (size_t)(p - bb));
            } else {
                sqlite3_reset(e->st_health);
                APPEND(p, "degraded\",\"db\":\"unreachable\",\"error\":\"query failed\"}");
                c_respond(s, 503, "Service Unavailable", "application/json", bb, (size_t)(p - bb));
            }
            return 1;
        }
        if (plen == 4 && memcmp(path, "feed", 4) == 0) {
            char fbuf[C_RESP_SIZE - 512];
            char *p = fbuf;
            APPEND(p, "{\"posts\":[");
            sqlite3_reset(e->st_feed);
            sqlite3_bind_int(e->st_feed, 1, C_FEED_LIMIT);
            int first = 1;
            while (sqlite3_step(e->st_feed) == SQLITE_ROW) {
                if (!first) *p++ = ',';
                first = 0;
                p = c_render_post(e->st_feed, p);
            }
            sqlite3_reset(e->st_feed);
            APPEND(p, "]}");
            size_t blen = (size_t)(p - fbuf);
            /* header first, then move the body into place */
            char hdr[192]; char *hp = hdr;
            APPEND(hp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
            hp = c_u64toa(hp, blen);
            if (s->keep_alive) APPEND(hp, "\r\nConnection: keep-alive\r\n\r\n");
            else               APPEND(hp, "\r\nConnection: close\r\n\r\n");
            size_t hlen = (size_t)(hp - hdr);
            memmove(s->resp_buf + hlen, fbuf, blen);
            memcpy(s->resp_buf, hdr, hlen);
            s->resp_ptr = s->resp_buf;
            s->resp_off = 0;
            s->resp_len = hlen + blen;
            s->state = C_WRITING;
            return 1;
        }
        if (plen > 6 && memcmp(path, "posts/", 6) == 0) {
            const char *idp = path + 6;
            size_t idn = plen - 6;
            int64_t id = 0;
            if (idn == 0 || idn > 18) { c_error(s, 400, "Bad Request", "{\"error\":\"invalid post id\"}"); return 1; }
            for (size_t i = 0; i < idn; i++) {
                if (idp[i] < '0' || idp[i] > '9') { id = -1; break; }
                id = id * 10 + (idp[i] - '0');
            }
            if (id < 0) { c_error(s, 400, "Bad Request", "{\"error\":\"invalid post id\"}"); return 1; }
            if (id == 0) { c_error(s, 400, "Bad Request", "{\"error\":\"invalid post id\"}"); return 1; }

            sqlite3_reset(e->st_post);
            sqlite3_bind_int64(e->st_post, 1, id);
            if (sqlite3_step(e->st_post) == SQLITE_ROW) {
                char pbuf[C_RESP_SIZE - 512];
                char *p = pbuf;
                APPEND(p, "{\"post\":");
                p = c_render_post(e->st_post, p);
                *p++ = '}';
                size_t blen = (size_t)(p - pbuf);
                char hdr[192]; char *hp = hdr;
                APPEND(hp, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
                hp = c_u64toa(hp, blen);
                if (s->keep_alive) APPEND(hp, "\r\nConnection: keep-alive\r\n\r\n");
                else               APPEND(hp, "\r\nConnection: close\r\n\r\n");
                size_t hlen = (size_t)(hp - hdr);
                memmove(s->resp_buf + hlen, pbuf, blen);
                memcpy(s->resp_buf, hdr, hlen);
                s->resp_ptr = s->resp_buf;
                s->resp_off = 0;
                s->resp_len = hlen + blen;
                s->state = C_WRITING;
            } else {
                c_error(s, 404, "Not Found", "{\"error\":\"post not found\"}");
            }
            sqlite3_reset(e->st_post);
            return 1;
        }
        c_not_found_route(s);
        return 1;
    }

    /* ---------------- writes: auth comes first ---------------- */
    int64_t user_id = 0;
    char username[C_USER_MAX];
    size_t ulen = 0;
    int rc = c_check_auth(e, req, hdr_len, &user_id, username, &ulen);
    if (rc == AUTH_MISSING) { c_error(s, 401, "Unauthorized", "{\"error\":\"missing bearer token\"}"); return 1; }
    if (rc == AUTH_BAD)     { c_error(s, 401, "Unauthorized", "{\"error\":\"invalid or expired token\"}"); return 1; }
    if (rc == AUTH_PAYLOAD) { c_error(s, 401, "Unauthorized", "{\"error\":\"invalid token payload\"}"); return 1; }

    if (plen == 5 && memcmp(path, "posts", 5) == 0) {
        /* POST /posts */
        if (!c_json_valid(body, body_end)) {
            c_error(s, 400, "Bad Request", "{\"error\":\"malformed JSON body\"}");
            return 1;
        }
        char kind = 0;
        const char *v = c_json_get(body, body_end, "body", 4, &kind);
        if (!v || kind != 's') { c_error(s, 400, "Bad Request", "{\"error\":\"body is required\"}"); return 1; }
        char tmp[C_REQ_SIZE];
        size_t tlen = 0;
        if (!c_json_dec_str(v, body_end, tmp, sizeof tmp, &tlen)) {
            c_error(s, 400, "Bad Request", "{\"error\":\"malformed JSON body\"}");
            return 1;
        }
        const char *tb = c_trim(tmp, &tlen);
        if (tlen == 0)   { c_error(s, 400, "Bad Request", "{\"error\":\"body is required\"}"); return 1; }
        if (tlen > 500)  { c_error(s, 400, "Bad Request", "{\"error\":\"body must be at most 500 characters\"}"); return 1; }
        if (e->pending_count >= C_BATCH) { c_error(s, 500, "Internal Server Error", "{\"error\":\"internal server error\"}"); return 1; }
        CPending *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx; pw->gen = s->gen; pw->kind = W_CREATE;
        pw->user_id = user_id; pw->post_id = 0;
        memcpy(pw->username, username, C_USER_MAX);
        memcpy(pw->body, tb, tlen);
        pw->body[tlen] = 0;
        pw->body_len = (int)tlen;
        s->state = C_QUEUED;
        return 2;
    }

    if (plen > 6 && memcmp(path, "posts/", 6) == 0) {
        const char *rest = path + 6;
        size_t rest_n = plen - 6;
        const char *slash = (const char *)memchr(rest, '/', rest_n);
        if (!slash) { c_not_found_route(s); return 1; }
        size_t idn = (size_t)(slash - rest);
        const char *tail = slash + 1;
        size_t tail_n = rest_n - idn - 1;
        if (tail_n != 4 || memcmp(tail, "like", 4) != 0) { c_not_found_route(s); return 1; }

        int64_t id = 0; int bad = 0;
        if (idn == 0 || idn > 18) bad = 1;
        for (size_t i = 0; !bad && i < idn; i++) {
            if (rest[i] < '0' || rest[i] > '9') { bad = 1; break; }
            id = id * 10 + (rest[i] - '0');
        }
        if (!bad && id <= 0) bad = 1;
        if (bad) { c_error(s, 400, "Bad Request", "{\"error\":\"invalid post id\"}"); return 1; }

        if (e->pending_count >= C_BATCH) { c_error(s, 500, "Internal Server Error", "{\"error\":\"internal server error\"}"); return 1; }
        CPending *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx; pw->gen = s->gen; pw->kind = W_LIKE;
        pw->user_id = user_id; pw->post_id = id;
        memcpy(pw->username, username, C_USER_MAX);
        pw->body_len = 0;
        s->state = C_QUEUED;
        return 2;
    }

    c_not_found_route(s);
    return 1;
}

/* feed bytes into a slot; -1 means close the connection */
static int c_feed(CEngine *e, int idx, const char *data, size_t len) {
    CSlot *s = &e->slots[idx];
    if (s->state == C_WRITING || s->state == C_QUEUED) {
        if (s->req_len + len > C_REQ_SIZE) return -1;
        memcpy(s->req_buf + s->req_len, data, len);
        s->req_len += len;
        return 0;
    }
    if (s->req_len > 0) {
        if (s->req_len + len > C_REQ_SIZE) return -1;
        memcpy(s->req_buf + s->req_len, data, len);
        s->req_len += len;
        int r = c_parse(e, idx, s->req_buf, s->req_len);
        if (r == 0) return 0;
        s->req_len = 0;
        if (r == 1) c_arm_send(e, idx);
        return 0;
    }
    int r = c_parse(e, idx, data, len);
    if (r == 0) {
        if (len > C_REQ_SIZE) return -1;
        memcpy(s->req_buf, data, len);
        s->req_len = len;
        return 0;
    }
    if (r == 1) c_arm_send(e, idx);
    return 0;
}

/* --------------------------------------------------------------- the entry */
hval hpbi_holy_challenge_start(hval port_val, hval db_path_val) {
    int port = (int)hp_val_to_int(port_val);
    if (port <= 0) port = 80;
    hstr *ds = hp_val_to_str(db_path_val);
    const char *db_path = (ds && ds->len) ? ds->data : "feed.db";
    const char *secret = getenv("JWT_SECRET");
    if (!secret || !*secret) secret = "twelve-dollar-challenge";

    CEngine e;
    memset(&e, 0, sizeof e);
    e.secret = secret;
    e.secret_len = strlen(secret);
    e.start_time = (int64_t)time(NULL);

    unsigned flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(db_path, &e.db, flags, NULL) != SQLITE_OK) {
        fprintf(stderr, "HolyPHP: cannot open %s: %s\n", db_path, sqlite3_errmsg(e.db));
        return hp_of_int(-1);
    }
    sqlite3_exec(e.db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA temp_store = MEMORY;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA mmap_size = 1073741824;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA cache_size = -64000;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA busy_timeout = 5000;", NULL, NULL, NULL);
    sqlite3_exec(e.db, "PRAGMA wal_autocheckpoint = 4096;", NULL, NULL, NULL);

    const char *prep =
        "SELECT p.id, p.body, p.created_at, u.username,"
        " (SELECT count(*) FROM likes l WHERE l.post_id = p.id)"
        " FROM posts p JOIN users u ON u.id = p.user_id"
        " WHERE p.id = ?;";
    const char *prep_feed =
        "SELECT p.id, p.body, p.created_at, u.username,"
        " (SELECT count(*) FROM likes l WHERE l.post_id = p.id)"
        " FROM posts p JOIN users u ON u.id = p.user_id"
        " ORDER BY p.created_at DESC, p.id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(e.db, "SELECT 1;", -1, &e.st_health, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, prep_feed, -1, &e.st_feed, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, prep, -1, &e.st_post, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, "INSERT INTO posts (user_id, body) VALUES (?, ?) RETURNING id, created_at;",
                           -1, &e.st_insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, "SELECT 1 FROM posts WHERE id = ?;", -1, &e.st_like_exists, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, "INSERT INTO likes (user_id, post_id) VALUES (?, ?)"
                                 " ON CONFLICT (user_id, post_id) DO NOTHING;",
                           -1, &e.st_like_ins, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, "BEGIN IMMEDIATE;", -1, &e.st_begin, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(e.db, "COMMIT;", -1, &e.st_commit, NULL) != SQLITE_OK) {
        fprintf(stderr, "HolyPHP: prepare failed: %s\n", sqlite3_errmsg(e.db));
        return hp_of_int(-1);
    }

    e.slots = (CSlot *)calloc(C_MAX_SLOTS, sizeof(CSlot));
    e.free_slots = (int *)malloc(C_MAX_SLOTS * sizeof(int));
    e.pending = (CPending *)malloc(C_BATCH * sizeof(CPending));
    if (!e.slots || !e.free_slots || !e.pending) return hp_of_int(-1);
    for (int i = 0; i < C_MAX_SLOTS; i++) {
        e.slots[i].fd = -1;
        e.slots[i].state = C_FREE;
        e.free_slots[i] = C_MAX_SLOTS - 1 - i;
    }
    e.free_count = C_MAX_SLOTS;

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) { perror("HolyPHP: socket"); return hp_of_int(-1); }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
    fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&sa, sizeof sa) != 0) { perror("HolyPHP: bind"); return hp_of_int(-1); }
    if (listen(listen_fd, 4096) != 0) { perror("HolyPHP: listen"); return hp_of_int(-1); }

    const char *mr = getenv("HPHP_MR");
    if (mr && *mr == '1') c_multishot = 1;

    struct io_uring ring;
    int r;
    const char *defer = getenv("HPHP_DEFER");
    if (defer && *defer == '0')
        r = io_uring_queue_init(C_RING, &ring, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN);
    else
        r = io_uring_queue_init(C_RING, &ring, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN);
    if (r < 0) r = io_uring_queue_init(C_RING, &ring, 0);
    if (r < 0) { fprintf(stderr, "HolyPHP: io_uring: %s\n", strerror(-r)); return hp_of_int(-1); }
    e.ring = &ring;

    if (c_multishot) {
        if (posix_memalign((void **)&e.buf_base, C_RECV_SIZE, (size_t)C_NBUF * C_RECV_SIZE) != 0)
            return hp_of_int(-1);
        int br = 0;
        e.br = io_uring_setup_buf_ring(&ring, C_NBUF, C_BGID, 0, &br);
        if (!e.br) { fprintf(stderr, "HolyPHP: buf ring: %s\n", strerror(-br)); return hp_of_int(-1); }
        for (int i = 0; i < C_NBUF; i++)
            io_uring_buf_ring_add(e.br, e.buf_base + (size_t)i * C_RECV_SIZE,
                                  C_RECV_SIZE, (unsigned short)i, C_NBUF - 1, i);
        io_uring_buf_ring_advance(e.br, C_NBUF);
    }

    c_arm_accept(&e, listen_fd);
    io_uring_submit(&ring);

    for (;;) {
        int ret = io_uring_submit_and_wait(&ring, 1);
        if (ret < 0 && errno != EINTR) {
            fprintf(stderr, "HolyPHP: submit_and_wait: %s\n", strerror(errno));
            break;
        }
        unsigned head;
        struct io_uring_cqe *cqe;
        e.cq_harvested = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            e.cq_harvested++;
            uint64_t ud = cqe->user_data;
            int res = cqe->res;
            unsigned cflags = cqe->flags;
            unsigned op = (unsigned)(ud & 3);
            int idx = (int)(ud >> 2);

            if (op == C_ACCEPT) {
                if (res >= 0) {
                    int one = 1;
                    setsockopt(res, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    if (e.free_count > 0) {
                        int si = e.free_slots[--e.free_count];
                        CSlot *s = &e.slots[si];
                        s->fd = res; s->state = C_READING; s->keep_alive = 1;
                        s->req_len = 0; s->resp_len = 0; s->resp_off = 0;
                        s->resp_ptr = s->resp_buf;
                        c_arm_recv(&e, si);
                    } else close(res);
                }
                if (!(cflags & IORING_CQE_F_MORE)) c_arm_accept(&e, listen_fd);
                continue;
            }
            if (idx < 0 || idx >= C_MAX_SLOTS) continue;
            CSlot *s = &e.slots[idx];
            if (s->state == C_FREE) continue;

            if (op == C_RECV) {
                unsigned bid = cflags >> IORING_CQE_BUFFER_SHIFT;
                int had_buf = (cflags & IORING_CQE_F_BUFFER) != 0;
                if (res == -ENOBUFS || res == -EAGAIN) {
                    if (!(cflags & IORING_CQE_F_MORE)) c_arm_recv(&e, idx);
                    continue;
                }
                if (res <= 0) {
                    if (had_buf) c_return_buf(&e, bid);
                    c_release(&e, idx);
                } else {
                    const char *data = had_buf ? e.buf_base + (size_t)bid * C_RECV_SIZE : s->recv_buf;
                    int bad = c_feed(&e, idx, data, (size_t)res) < 0;
                    if (had_buf) c_return_buf(&e, bid);
                    if (bad) c_release(&e, idx);
                    else if (!(cflags & IORING_CQE_F_MORE) && e.slots[idx].state != C_FREE)
                        c_arm_recv(&e, idx);
                }
            } else if (op == C_SEND) {
                if (res < 0 && res != -EAGAIN) {
                    c_release(&e, idx);
                } else if (res <= 0) {
                    if (res == 0) c_release(&e, idx);
                    else c_arm_send(&e, idx);
                } else {
                    s->resp_off += (size_t)res;
                    if (s->resp_off < s->resp_len) {
                        c_arm_send(&e, idx);
                    } else {
                        s->resp_off = 0; s->resp_len = 0;
                        if (!s->keep_alive) c_release(&e, idx);
                        else if (s->req_len > 0) {
                            size_t n = s->req_len; s->req_len = 0; s->state = C_READING;
                            if (c_feed(&e, idx, s->req_buf, n) < 0) c_release(&e, idx);
                        } else s->state = C_READING;
                    }
                }
            }
        }
        if (e.cq_harvested) {
            io_uring_cq_advance(&ring, e.cq_harvested);
            e.cq_harvested = 0;
        }

        /* ---- group commit: one transaction for the whole batch ---- */
        if (e.pending_count > 0) {
            sqlite3_step(e.st_begin);
            sqlite3_reset(e.st_begin);
            int failed = 0;

            for (int i = 0; i < e.pending_count; i++) {
                CPending *pw = &e.pending[i];
                if (pw->slot_idx < 0 || pw->slot_idx >= C_MAX_SLOTS) continue;
                CSlot *s = &e.slots[pw->slot_idx];
                if (s->state != C_QUEUED || s->gen != pw->gen) continue;

                char out[C_RESP_SIZE - 512];
                char *p = out;
                int code = 200; const char *reason = "OK";

                if (pw->kind == W_CREATE) {
                    sqlite3_reset(e.st_insert);
                    sqlite3_bind_int64(e.st_insert, 1, pw->user_id);
                    sqlite3_bind_text(e.st_insert, 2, pw->body, pw->body_len, SQLITE_STATIC);
                    if (sqlite3_step(e.st_insert) == SQLITE_ROW) {
                        int64_t nid = sqlite3_column_int64(e.st_insert, 0);
                        const unsigned char *ct = sqlite3_column_text(e.st_insert, 1);
                        int cl = sqlite3_column_bytes(e.st_insert, 1);
                        APPEND(p, "{\"post\":");
                        p = c_render_new_post(p, nid, pw->body, (size_t)pw->body_len,
                                              (const char *)(ct ? ct : (const unsigned char *)""), (size_t)(ct ? cl : 0),
                                              pw->username, strlen(pw->username));
                        *p++ = '}';
                        code = 201; reason = "Created";
                    } else failed = 1;
                    sqlite3_reset(e.st_insert);
                } else {
                    sqlite3_reset(e.st_like_exists);
                    sqlite3_bind_int64(e.st_like_exists, 1, pw->post_id);
                    if (sqlite3_step(e.st_like_exists) == SQLITE_ROW) {
                        sqlite3_reset(e.st_like_exists);
                        sqlite3_reset(e.st_like_ins);
                        sqlite3_bind_int64(e.st_like_ins, 1, pw->user_id);
                        sqlite3_bind_int64(e.st_like_ins, 2, pw->post_id);
                        if (sqlite3_step(e.st_like_ins) == SQLITE_DONE) {
                            int already = sqlite3_changes(e.db) == 0;
                            APPEND(p, "{\"liked\":true,\"already_liked\":");
                            if (already) APPEND(p, "true,"); else APPEND(p, "false,");
                            APPEND(p, "\"post_id\":");
                            p = c_i64toa(p, pw->post_id);
                            *p++ = '}';
                            code = already ? 200 : 201;
                            reason = already ? "OK" : "Created";
                        } else failed = 1;
                        sqlite3_reset(e.st_like_ins);
                    } else {
                        sqlite3_reset(e.st_like_exists);
                        c_error(s, 404, "Not Found", "{\"error\":\"post not found\"}");
                        c_arm_send(&e, pw->slot_idx);
                        continue;
                    }
                }
                if (failed) {
                    c_error(s, 500, "Internal Server Error", "{\"error\":\"internal server error\"}");
                } else {
                    c_respond(s, code, reason, "application/json", out, (size_t)(p - out));
                }
                c_arm_send(&e, pw->slot_idx);
            }

            int crc = sqlite3_step(e.st_commit);
            if (crc != SQLITE_DONE && crc != SQLITE_OK) {
                fprintf(stderr, "HolyPHP: commit failed: %s\n", sqlite3_errmsg(e.db));
                sqlite3_exec(e.db, "ROLLBACK;", NULL, NULL, NULL);
            }
            sqlite3_reset(e.st_commit);
            e.pending_count = 0;
        }
    }
    return hp_of_int(0);
}
#else
hval hpbi_holy_challenge_start(hval port_val, hval db_path_val) {
    (void)port_val; (void)db_path_val;
    hp_throw_str("holy_challenge_start() requires Linux io_uring");
    return hp_of_int(-1);
}
#endif
