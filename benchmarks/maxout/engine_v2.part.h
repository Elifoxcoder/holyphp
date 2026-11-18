/* ================= HolyPHP MaxOut io_uring Engine v2 ==================
 *
 * Same wire protocol + SQLite schema as the Rust reference server, but the
 * loop is tuned for one vCPU:
 *
 *   - multi-shot accept:    one SQE accepts connections until it errors
 *   - multi-shot recv:      one SQE per connection, kernel keeps it armed
 *   - provided buffers:     recv lands in a pre-registered 4 KiB buf ring,
 *                           no per-request read buffer bookkeeping/syscall
 *   - DEFER_TASKRUN:        task_work runs in this thread, not in a kworker
 *   - prepared SQLite stmts: no parse/prepare per request
 *   - group commit:         every write that arrived in the same CQE batch is
 *                           one BEGIN..COMMIT, so one fsync-ish WAL append
 *   - feed body cache:      rebuild only after a write invalidated it
 *   - zero-copy JSON:       sqlite3_column_text() bytes are escaped straight
 *                           into the response buffer, no intermediate String
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

#define MAX_SLOTS 2048
#define REQ_BUF_SIZE 4096
#define RESP_BUF_SIZE 16384
#define RECV_BUF_SIZE 4096
#define NBUF 1024
#define BGID 1
#define BATCH_MAX 4096
#define RING_ENTRIES 1024
#define FEED_LIMIT 20
#define FEED_CACHE_SIZE 8192

enum {
    SLOT_FREE = 0,
    SLOT_READING = 1,
    SLOT_WRITING = 2,
    SLOT_QUEUED_WRITE = 3
};

enum {
    OP_ACCEPT = 0,
    OP_RECV = 1,
    OP_SEND = 2
};

#define TAG(idx, op) (((uint64_t)(idx) << 2) | (uint64_t)(op))

typedef struct {
    int fd;
    int state;
    int keep_alive;
    size_t req_len;     /* bytes buffered while a response is in flight */
    size_t resp_len;
    size_t resp_off;
    char req_buf[REQ_BUF_SIZE];
    char resp_buf[RESP_BUF_SIZE];
} MaxOutSlot;

typedef struct {
    int slot_idx;
    int is_like;
    int64_t id;
    int64_t created_at;
    char author[64];
    char content[256];
} MaxOutPendingWrite;

static inline char *fast_u64toa(char *p, uint64_t v) {
    if (v == 0) { *p++ = '0'; return p; }
    char tmp[24];
    int i = 0;
    while (v > 0) { tmp[i++] = (char)(v % 10) + '0'; v /= 10; }
    while (i > 0) *p++ = tmp[--i];
    return p;
}

static inline char *fast_i64toa(char *p, int64_t v) {
    if (v < 0) { *p++ = '-'; v = -v; }
    return fast_u64toa(p, (uint64_t)v);
}

static inline void escape_and_write_json(char **dst, const char *src, size_t len) {
    char *p = *dst;
    *p++ = '"';
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '"') { *p++ = '\\'; *p++ = '"'; }
        else if (c == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else if (c == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (c == '\r') { *p++ = '\\'; *p++ = 'r'; }
        else if (c == '\t') { *p++ = '\\'; *p++ = 't'; }
        else *p++ = c;
    }
    *p++ = '"';
    *dst = p;
}

/* Write the HTTP head + body into the slot's response buffer. */
static inline void make_response(MaxOutSlot *s, const char *body, size_t body_len, int is_json) {
    static const char hdr[] = "HTTP/1.1 200 OK\r\nServer: HolyPHP-MaxOut\r\nContent-Type: ";
    static const char ka[] = "\r\nConnection: keep-alive\r\n\r\n";
    static const char cl[] = "\r\nConnection: close\r\n\r\n";
    char *p = s->resp_buf;
    memcpy(p, hdr, sizeof hdr - 1); p += sizeof hdr - 1;
    if (is_json) { memcpy(p, "application/json\r\n", 18); p += 18; }
    else         { memcpy(p, "text/plain\r\n", 12); p += 12; }
    memcpy(p, "Content-Length: ", 16); p += 16;
    p = fast_u64toa(p, body_len);
    if (s->keep_alive) { memcpy(p, ka, sizeof ka - 1); p += sizeof ka - 1; }
    else               { memcpy(p, cl, sizeof cl - 1); p += sizeof cl - 1; }
    memcpy(p, body, body_len);
    p += body_len;
    s->resp_off = 0;
    s->resp_len = (size_t)(p - s->resp_buf);
    s->state = SLOT_WRITING;
}

static inline void make_not_found(MaxOutSlot *s) {
    static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    memcpy(s->resp_buf, nf, sizeof nf - 1);
    s->resp_off = 0;
    s->resp_len = sizeof nf - 1;
    s->state = SLOT_WRITING;
}

static inline void *mem_find(const void *hay, size_t hay_len, const void *needle, size_t needle_len) {
    if (needle_len == 0 || hay_len < needle_len) return NULL;
    const char *h = (const char *)hay;
    const char first = ((const char *)needle)[0];
    const char *end = h + hay_len - needle_len;
    for (const char *p = h; p <= end; p++) {
        p = (const char *)memchr(p, first, (size_t)(end - p) + 1);
        if (!p) return NULL;
        if (memcmp(p, needle, needle_len) == 0) return (void *)p;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* request handling                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *feed, *insert, *like, *begin, *commit;
    MaxOutSlot *slots;
    int *free_slots;
    int free_count;
    MaxOutPendingWrite *pending;
    int pending_count;
    char feed_json[FEED_CACHE_SIZE];
    size_t feed_len;
    int feed_valid;
    struct io_uring *ring;
    struct io_uring_buf_ring *br;
} MaxOutEngine;

static void build_feed(MaxOutEngine *e) {
    if (e->feed_valid) return;

    sqlite3_reset(e->feed);
    sqlite3_bind_int(e->feed, 1, FEED_LIMIT);

    char *jp = e->feed_json;
    char *limit = e->feed_json + sizeof(e->feed_json) - 512;
    *jp++ = '[';
    int first = 1;

    while (sqlite3_step(e->feed) == SQLITE_ROW) {
        if (jp >= limit) break;             /* never overrun the cache */
        if (!first) *jp++ = ',';
        first = 0;

        int64_t id = sqlite3_column_int64(e->feed, 0);
        const char *author = (const char *)sqlite3_column_text(e->feed, 1);
        int author_len = sqlite3_column_bytes(e->feed, 1);
        const char *content = (const char *)sqlite3_column_text(e->feed, 2);
        int content_len = sqlite3_column_bytes(e->feed, 2);
        int64_t likes = sqlite3_column_int64(e->feed, 3);
        int64_t created_at = sqlite3_column_int64(e->feed, 4);

        memcpy(jp, "{\"id\":", 6); jp += 6;
        jp = fast_i64toa(jp, id);
        memcpy(jp, ",\"author\":", 10); jp += 10;
        escape_and_write_json(&jp, author ? author : "", (size_t)author_len);
        memcpy(jp, ",\"content\":", 11); jp += 11;
        escape_and_write_json(&jp, content ? content : "", (size_t)content_len);
        memcpy(jp, ",\"likes\":", 9); jp += 9;
        jp = fast_i64toa(jp, likes);
        memcpy(jp, ",\"created_at\":", 14); jp += 14;
        jp = fast_i64toa(jp, created_at);
        *jp++ = '}';
    }
    *jp++ = ']';
    e->feed_len = (size_t)(jp - e->feed_json);
    e->feed_valid = 1;
}

static void arm_send(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (!sqe) return;
    io_uring_prep_send(sqe, s->fd, s->resp_buf + s->resp_off,
                       s->resp_len - s->resp_off, MSG_NOSIGNAL);
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_SEND));
}

/* returns 1 = response ready, 2 = write queued, 0 = need more bytes */
static int parse_request(MaxOutEngine *e, int idx, const char *req, size_t len) {
    MaxOutSlot *s = &e->slots[idx];

    char *hdr_end_p = (char *)mem_find(req, len, "\r\n\r\n", 4);
    if (!hdr_end_p) return 0;
    size_t hdr_len = (size_t)(hdr_end_p - req) + 4;

    size_t body_len = 0;
    char *cl = (char *)mem_find(req, hdr_len, "\r\nContent-Length: ", 18);
    if (cl) body_len = (size_t)strtoul(cl + 18, NULL, 10);
    if (len < hdr_len + body_len) return 0;     /* body split across reads */

    s->keep_alive = mem_find(req, hdr_len, "Connection: close", 17) == NULL;

    const char *body = req + hdr_len;

    if (len >= 11 && memcmp(req, "GET /health", 11) == 0) {
        make_response(s, "{\"status\":\"ok\"}", 15, 1);
        return 1;
    }

    if ((len >= 9 && memcmp(req, "GET /feed", 9) == 0) ||
        (len >= 10 && memcmp(req, "GET /posts", 10) == 0)) {
        build_feed(e);
        make_response(s, e->feed_json, e->feed_len, 1);
        return 1;
    }

    if (len >= 11 && memcmp(req, "POST /posts", 11) == 0) {
        if (e->pending_count >= BATCH_MAX) { make_response(s, "{\"error\":\"busy\"}", 17, 1); return 1; }
        MaxOutPendingWrite *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx;
        pw->is_like = 0;
        pw->created_at = (int64_t)time(NULL);
        strcpy(pw->author, "anonymous");
        strcpy(pw->content, "hello world");

        char *a = (char *)mem_find(body, body_len, "\"author\":\"", 10);
        if (a) {
            char *ap = a + 10;
            char *ae = (char *)mem_find(ap, (size_t)(body + body_len - ap), "\"", 1);
            if (ae) {
                size_t n = (size_t)(ae - ap);
                if (n >= sizeof pw->author) n = sizeof pw->author - 1;
                memcpy(pw->author, ap, n);
                pw->author[n] = '\0';
            }
        }
        char *c = (char *)mem_find(body, body_len, "\"content\":\"", 11);
        if (c) {
            char *cp = c + 11;
            char *ce = (char *)mem_find(cp, (size_t)(body + body_len - cp), "\"", 1);
            if (ce) {
                size_t n = (size_t)(ce - cp);
                if (n >= sizeof pw->content) n = sizeof pw->content - 1;
                memcpy(pw->content, cp, n);
                pw->content[n] = '\0';
            }
        }
        s->state = SLOT_QUEUED_WRITE;
        return 2;
    }

    if (len >= 10 && memcmp(req, "POST /like", 10) == 0) {
        if (e->pending_count >= BATCH_MAX) { make_response(s, "{\"error\":\"busy\"}", 17, 1); return 1; }
        MaxOutPendingWrite *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx;
        pw->is_like = 1;
        pw->id = 1;
        char *q = (char *)mem_find(req, len, "?id=", 4);
        if (q) {
            pw->id = strtoll(q + 4, NULL, 10);
            if (pw->id <= 0) pw->id = 1;
        }
        s->state = SLOT_QUEUED_WRITE;
        return 2;
    }

    make_not_found(s);
    return 1;
}

/* Feed bytes into a slot. Returns -1 if the connection must be closed. */
static int feed_bytes(MaxOutEngine *e, int idx, const char *data, size_t len) {
    MaxOutSlot *s = &e->slots[idx];

    /* A response is in flight (or a write is waiting for the commit):
     * buffer the request instead of clobbering resp_buf. */
    if (s->state == SLOT_WRITING || s->state == SLOT_QUEUED_WRITE) {
        if (s->req_len + len > REQ_BUF_SIZE) return -1;
        memcpy(s->req_buf + s->req_len, data, len);
        s->req_len += len;
        return 0;
    }

    if (s->req_len > 0) {
        if (s->req_len + len > REQ_BUF_SIZE) return -1;
        memcpy(s->req_buf + s->req_len, data, len);
        s->req_len += len;
        data = s->req_buf;
        len = s->req_len;
        int r = parse_request(e, idx, data, len);
        if (r == 0) return 0;                 /* keep accumulating */
        s->req_len = 0;
        if (r == 1) arm_send(e, idx);
        return 0;
    }

    int r = parse_request(e, idx, data, len);
    if (r == 0) {
        if (len > REQ_BUF_SIZE) return -1;
        memcpy(s->req_buf, data, len);
        s->req_len = len;
        return 0;
    }
    if (r == 1) arm_send(e, idx);
    return 0;
}

static void arm_recv(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (!sqe) return;
    io_uring_prep_recv_multishot(sqe, s->fd, NULL, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BGID;
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_RECV));
}

static inline void release_slot(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    s->state = SLOT_FREE;
    e->free_slots[e->free_count++] = idx;
}

static inline void return_buffer(MaxOutEngine *e, unsigned bid) {
    char *buf = (char *)e->br + 0; /* silence unused warnings on old compilers */
    (void)buf;
    void *addr = *(void **)0; (void)addr;
}

hval hpbi_holy_maxout_start(hval port_val, hval db_path_val);
