/* ================= HolyPHP MaxOut io_uring Engine v3 ==================
 *
 * Same wire protocol + SQLite schema as the Rust reference server, but the
 * loop is tuned for one vCPU:
 *
 *   - multi-shot accept:   one SQE keeps accepting until the kernel says stop
 *   - single-shot recv:    measured faster than multishot+provided buffers on
 *                          this kernel (see benchmarks/maxout/RESULTS.md):
 *                          no buffer-ring atomics per request, the kernel
 *                          copies straight into the slot's own receive buffer.
 *                          HPHP_MR=1 switches multishot + provided buffers on.
 *   - DEFER_TASKRUN:       task_work runs in this thread, not in a kworker
 *   - prepared statements: no SQL parse/prepare per request
 *   - group commit:        all writes from one CQE batch share BEGIN..COMMIT
 *   - feed cache:          the whole HTTP response is pre-assembled once and
 *                          handed to send() by pointer - no per-request copy,
 *                          no per-request header formatting. Invalidated only
 *                          by a write.
 *   - health cache:        same trick for the constant /health answer
 *   - zero-copy JSON:      sqlite3_column_text() bytes go straight into the
 *                          response buffer, no intermediate String
 *   - SQ-full recovery:    a full submission queue is drained and retried
 *                          instead of dropping the SQE (a dropped SQE stalls
 *                          its connection for seconds)
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
#define RING_ENTRIES 4096
#define FEED_LIMIT 20
#define FEED_CACHE_SIZE 8192
#define HEALTH_CACHE_SIZE 512

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
    uint32_t gen;
    size_t req_len;     /* bytes buffered while a response is in flight */
    size_t resp_len;
    size_t resp_off;
    const char *resp_ptr;
    char recv_buf[RECV_BUF_SIZE];
    char req_buf[REQ_BUF_SIZE];
    char resp_buf[RESP_BUF_SIZE];
} MaxOutSlot;

typedef struct {
    int slot_idx;
    int is_like;
    uint32_t gen;
    int64_t id;
    int64_t created_at;
    char author[64];
    char content[256];
} MaxOutPendingWrite;

static int g_multishot_recv = 0;   /* HPHP_MR=1 -> multishot recv + buf ring */
static int g_feed_cache = 1;       /* HPHP_CACHE=0 -> always rebuild feed */

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
    char feed_http[FEED_CACHE_SIZE + 512];
    size_t feed_http_len;
    char health_http[HEALTH_CACHE_SIZE];
    size_t health_http_len;
    int feed_valid;
    size_t cq_harvested;    /* CQEs read so far, not yet released */
    struct io_uring *ring;
    struct io_uring_buf_ring *br;
    char *buf_base;
} MaxOutEngine;

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
    s->resp_ptr = s->resp_buf;
    s->resp_len = (size_t)(p - s->resp_buf);
    s->state = SLOT_WRITING;
}

static inline void make_not_found(MaxOutSlot *s) {
    static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    memcpy(s->resp_buf, nf, sizeof nf - 1);
    s->resp_off = 0;
    s->resp_ptr = s->resp_buf;
    s->resp_len = sizeof nf - 1;
    s->state = SLOT_WRITING;
}

static inline void *mem_find(const void *hay, size_t hay_len, const void *needle, size_t needle_len) {
    if (needle_len == 0 || hay_len < needle_len) return NULL;
    const char *h = (const char *)hay;
    const char first = ((const char *)needle)[0];
    const char *end = h + hay_len - needle_len;
    const char *p = h;
    while (p <= end) {
        p = (const char *)memchr(p, first, (size_t)(end - p) + 1);
        if (!p) return NULL;
        if (memcmp(p, needle, needle_len) == 0) return (void *)p;
        p++;
    }
    return NULL;
}

/* An SQE, draining the submission queue first if it is full. A dropped SQE
 * would leave its connection waiting for an event that never comes, so the
 * entries already harvested are released back to the kernel before the
 * submit - otherwise the ring could overflow while we are still reading it. */
static inline struct io_uring_sqe *get_sqe(MaxOutEngine *e) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (sqe) return sqe;
    if (e->cq_harvested) {
        io_uring_cq_advance(e->ring, e->cq_harvested);
        e->cq_harvested = 0;
    }
    io_uring_submit(e->ring);
    return io_uring_get_sqe(e->ring);
}

static void arm_send(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = get_sqe(e);
    if (!sqe) return;
    io_uring_prep_send(sqe, s->fd, s->resp_ptr + s->resp_off,
                       s->resp_len - s->resp_off, MSG_NOSIGNAL);
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_SEND));
}

static void arm_recv(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = get_sqe(e);
    if (!sqe) return;
    if (g_multishot_recv) {
        io_uring_prep_recv_multishot(sqe, s->fd, NULL, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = BGID;
    } else {
        io_uring_prep_recv(sqe, s->fd, s->recv_buf, RECV_BUF_SIZE, 0);
    }
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_RECV));
}

static void arm_accept(MaxOutEngine *e, int listen_fd) {
    struct io_uring_sqe *sqe = get_sqe(e);
    if (!sqe) return;
    io_uring_prep_multishot_accept(sqe, listen_fd, NULL, NULL, 0);
    io_uring_sqe_set_data64(sqe, TAG(0, OP_ACCEPT));
}

static inline void return_buffer(MaxOutEngine *e, unsigned bid) {
    io_uring_buf_ring_add(e->br, e->buf_base + (size_t)bid * RECV_BUF_SIZE,
                          RECV_BUF_SIZE, (unsigned short)bid, NBUF - 1, 0);
    io_uring_buf_ring_advance(e->br, 1);
}

static inline void release_slot(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    if (s->state == SLOT_FREE) return;
    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
    s->state = SLOT_FREE;
    s->gen++;
    e->free_slots[e->free_count++] = idx;
}

/* Render the whole HTTP response for a request whose answer never changes. */
static void cache_static_response(const char *body, size_t body_len,
                                  char *out, size_t *out_len) {
    static MaxOutSlot tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.keep_alive = 1;
    make_response(&tmp, body, body_len, 1);
    memcpy(out, tmp.resp_buf, tmp.resp_len);
    *out_len = tmp.resp_len;
}

static void build_feed(MaxOutEngine *e) {
    if (g_feed_cache && e->feed_valid) return;

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

    /* Pre-assemble the whole wire response once: subsequent feed hits send
     * these bytes directly, with no header formatting and no copy of the
     * body. Deterministic, so Content-Length is baked in. */
    cache_static_response(e->feed_json, e->feed_len, e->feed_http, &e->feed_http_len);
    e->feed_valid = 1;
}

/* returns 1 = response ready, 2 = write queued, 0 = need more bytes */
static int parse_request(MaxOutEngine *e, int idx, const char *req, size_t len) {
    MaxOutSlot *s = &e->slots[idx];

    char *hdr_end_p = (char *)mem_find(req, len, "\r\n\r\n", 4);
    if (!hdr_end_p) return 0;
    size_t hdr_len = (size_t)(hdr_end_p - req) + 4;

    /* Method first: only a body-bearing method can have a Content-Length,
     * and hunting for it costs a scan of the whole header. */
    int is_post = (len >= 5 && memcmp(req, "POST ", 5) == 0);
    int is_get  = (len >= 4 && memcmp(req, "GET ", 4) == 0);

    s->keep_alive = mem_find(req, hdr_len, "Connection: close", 17) == NULL;

    size_t body_len = 0;
    if (is_post) {
        char *cl = (char *)mem_find(req, hdr_len, "\r\nContent-Length: ", 18);
        if (cl) body_len = (size_t)strtoul(cl + 18, NULL, 10);
        if (len < hdr_len + body_len) return 0;   /* body split across reads */
    }

    const char *body = req + hdr_len;

    if (is_get && len >= 11 && memcmp(req, "GET /health", 11) == 0) {
        if (s->keep_alive && e->health_http_len) {
            s->resp_ptr = e->health_http;
            s->resp_off = 0;
            s->resp_len = e->health_http_len;
            s->state = SLOT_WRITING;
        } else {
            make_response(s, "{\"status\":\"ok\"}", 15, 1);
        }
        return 1;
    }

    if ((is_get && len >= 9 && memcmp(req, "GET /feed", 9) == 0) ||
        (is_get && len >= 10 && memcmp(req, "GET /posts", 10) == 0)) {
        build_feed(e);
        if (s->keep_alive && e->feed_http_len) {
            s->resp_ptr = e->feed_http;
            s->resp_off = 0;
            s->resp_len = e->feed_http_len;
            s->state = SLOT_WRITING;
        } else {
            make_response(s, e->feed_json, e->feed_len, 1);
        }
        return 1;
    }

    if (is_post && len >= 11 && memcmp(req, "POST /posts", 11) == 0) {
        if (e->pending_count >= BATCH_MAX) {
            make_response(s, "{\"error\":\"busy\"}", 17, 1);
            return 1;
        }
        MaxOutPendingWrite *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx;
        pw->is_like = 0;
        pw->gen = s->gen;
        pw->id = 0;
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

    if (is_post && len >= 10 && memcmp(req, "POST /like", 10) == 0) {
        if (e->pending_count >= BATCH_MAX) {
            make_response(s, "{\"error\":\"busy\"}", 17, 1);
            return 1;
        }
        MaxOutPendingWrite *pw = &e->pending[e->pending_count++];
        pw->slot_idx = idx;
        pw->is_like = 1;
        pw->gen = s->gen;
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

    /* A response is in flight (or a write waits for the commit): buffer the
     * request instead of clobbering resp_buf. */
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

hval hpbi_holy_maxout_start(hval port_val, hval db_path_val) {
    int port = (int)hp_val_to_int(port_val);
    if (port <= 0) port = 8080;
    hstr *ds = hp_val_to_str(db_path_val);
    const char *db_path = ds && ds->len ? ds->data : "benchmark.db";

    MaxOutEngine e;
    memset(&e, 0, sizeof e);

    /* ---- SQLite: WAL, NORMAL sync, prepared statements ---- */
    if (sqlite3_open_v2(db_path, &e.db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                        NULL) != SQLITE_OK) {
        fprintf(stderr, "HolyPHP: cannot open %s: %s\n", db_path, sqlite3_errmsg(e.db));
        return hp_of_int(-1);
    }
    char *errmsg = NULL;
    sqlite3_exec(e.db, "PRAGMA journal_mode = WAL;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA synchronous = NORMAL;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA cache_size = -64000;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA temp_store = MEMORY;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA mmap_size = 268435456;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA busy_timeout = 5000;", NULL, NULL, &errmsg);
    sqlite3_exec(e.db, "PRAGMA wal_autocheckpoint = 4096;", NULL, NULL, &errmsg);

    sqlite3_exec(e.db,
        "CREATE TABLE IF NOT EXISTS posts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "author TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "likes INTEGER NOT NULL DEFAULT 0,"
        "created_at INTEGER NOT NULL);", NULL, NULL, &errmsg);
    sqlite3_exec(e.db,
        "INSERT INTO posts (author, content, likes, created_at) "
        "SELECT 'benchmark_user', 'Initial benchmark post content', 10, 1698500000 "
        "WHERE NOT EXISTS (SELECT 1 FROM posts LIMIT 1);", NULL, NULL, &errmsg);

    sqlite3_prepare_v2(e.db, "SELECT id, author, content, likes, created_at FROM posts ORDER BY id DESC LIMIT ?;", -1, &e.feed, NULL);
    sqlite3_prepare_v2(e.db, "INSERT INTO posts(author, content, likes, created_at) VALUES(?, ?, 0, ?);", -1, &e.insert, NULL);
    sqlite3_prepare_v2(e.db, "UPDATE posts SET likes = likes + 1 WHERE id = ?;", -1, &e.like, NULL);
    sqlite3_prepare_v2(e.db, "BEGIN IMMEDIATE;", -1, &e.begin, NULL);
    sqlite3_prepare_v2(e.db, "COMMIT;", -1, &e.commit, NULL);

    /* ---- connection slots ---- */
    e.slots = (MaxOutSlot *)calloc(MAX_SLOTS, sizeof(MaxOutSlot));
    e.free_slots = (int *)malloc(MAX_SLOTS * sizeof(int));
    e.pending = (MaxOutPendingWrite *)malloc(BATCH_MAX * sizeof(MaxOutPendingWrite));
    if (!e.slots || !e.free_slots || !e.pending) {
        fprintf(stderr, "HolyPHP: out of memory\n");
        return hp_of_int(-1);
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        e.slots[i].fd = -1;
        e.slots[i].state = SLOT_FREE;
        e.free_slots[i] = MAX_SLOTS - 1 - i;
    }
    e.free_count = MAX_SLOTS;

    /* ---- static answers, pre-assembled as complete HTTP messages ---- */
    cache_static_response("{\"status\":\"ok\"}", 15,
                          e.health_http, &e.health_http_len);

    /* ---- listening socket ---- */
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) { perror("HolyPHP: socket"); return hp_of_int(-1); }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
    fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof saddr);
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&saddr, sizeof saddr) != 0) {
        perror("HolyPHP: bind"); return hp_of_int(-1);
    }
    if (listen(listen_fd, 4096) != 0) {
        perror("HolyPHP: listen"); return hp_of_int(-1);
    }

    /* ---- io_uring ---- */
    const char *mr = getenv("HPHP_MR");
    if (mr && *mr == '1') g_multishot_recv = 1;
    const char *fc = getenv("HPHP_CACHE");
    if (fc && *fc == '0') g_feed_cache = 0;

    struct io_uring ring;
    int rc;
    const char *defer = getenv("HPHP_DEFER");
    if (defer && *defer == '0') {
        rc = io_uring_queue_init(RING_ENTRIES, &ring, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN);
        if (rc < 0) rc = io_uring_queue_init(RING_ENTRIES, &ring, 0);
    } else {
        rc = io_uring_queue_init(RING_ENTRIES, &ring,
                                 IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN);
        if (rc < 0)
            rc = io_uring_queue_init(RING_ENTRIES, &ring,
                                     IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN);
        if (rc < 0)
            rc = io_uring_queue_init(RING_ENTRIES, &ring, 0);
    }
    if (rc < 0) {
        fprintf(stderr, "HolyPHP: io_uring_queue_init: %s\n", strerror(-rc));
        return hp_of_int(-1);
    }
    e.ring = &ring;

    if (g_multishot_recv) {
        if (posix_memalign((void **)&e.buf_base, RECV_BUF_SIZE,
                           (size_t)NBUF * RECV_BUF_SIZE) != 0) {
            fprintf(stderr, "HolyPHP: cannot allocate receive buffers\n");
            return hp_of_int(-1);
        }
        int br_ret = 0;
        e.br = io_uring_setup_buf_ring(&ring, NBUF, BGID, 0, &br_ret);
        if (!e.br) {
            fprintf(stderr, "HolyPHP: io_uring_setup_buf_ring: %s\n", strerror(-br_ret));
            return hp_of_int(-1);
        }
        for (int i = 0; i < NBUF; i++) {
            io_uring_buf_ring_add(e.br, e.buf_base + (size_t)i * RECV_BUF_SIZE,
                                  RECV_BUF_SIZE, (unsigned short)i, NBUF - 1, i);
        }
        io_uring_buf_ring_advance(e.br, NBUF);
    }

    arm_accept(&e, listen_fd);
    io_uring_submit(&ring);

    /* ---- event loop ---- */
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

            if (op == OP_ACCEPT) {
                if (res >= 0) {
                    int fd = res;
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    if (e.free_count > 0) {
                        int si = e.free_slots[--e.free_count];
                        MaxOutSlot *s = &e.slots[si];
                        s->fd = fd;
                        s->state = SLOT_READING;
                        s->keep_alive = 1;
                        s->req_len = 0;
                        s->resp_len = 0;
                        s->resp_off = 0;
                        s->resp_ptr = s->resp_buf;
                        arm_recv(&e, si);
                    } else {
                        close(fd);
                    }
                }
                if (!(cflags & IORING_CQE_F_MORE)) arm_accept(&e, listen_fd);

            } else if (idx >= 0 && idx < MAX_SLOTS) {
                MaxOutSlot *s = &e.slots[idx];
                if (s->state == SLOT_FREE) continue;

                if (op == OP_RECV) {
                    unsigned bid = cflags >> IORING_CQE_BUFFER_SHIFT;
                    int had_buf = (cflags & IORING_CQE_F_BUFFER) != 0;

                    if (res == -ENOBUFS || res == -EAGAIN) {
                        if (!(cflags & IORING_CQE_F_MORE)) arm_recv(&e, idx);
                        continue;
                    }
                    if (res <= 0) {
                        if (had_buf) return_buffer(&e, bid);
                        release_slot(&e, idx);
                    } else {
                        const char *data = had_buf
                            ? e.buf_base + (size_t)bid * RECV_BUF_SIZE
                            : s->recv_buf;
                        int bad = feed_bytes(&e, idx, data, (size_t)res) < 0;
                        if (had_buf) return_buffer(&e, bid);
                        if (bad) {
                            release_slot(&e, idx);
                        } else if (!(cflags & IORING_CQE_F_MORE) &&
                                   e.slots[idx].state != SLOT_FREE) {
                            arm_recv(&e, idx);
                        }
                    }

                } else if (op == OP_SEND) {
                    if (res < 0 && res != -EAGAIN) {
                        release_slot(&e, idx);
                    } else if (res == -EAGAIN || res == 0) {
                        if (res == 0) { release_slot(&e, idx); }
                        else arm_send(&e, idx);
                    } else {
                        s->resp_off += (size_t)res;
                        if (s->resp_off < s->resp_len) {
                            arm_send(&e, idx);
                        } else {
                            s->resp_off = 0;
                            s->resp_len = 0;
                            if (!s->keep_alive) {
                                release_slot(&e, idx);
                            } else if (s->req_len > 0) {
                                /* a pipelined request waited behind the send */
                                size_t n = s->req_len;
                                s->req_len = 0;
                                s->state = SLOT_READING;
                                if (feed_bytes(&e, idx, s->req_buf, n) < 0)
                                    release_slot(&e, idx);
                            } else {
                                s->state = SLOT_READING;
                            }
                        }
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
            sqlite3_step(e.begin);
            sqlite3_reset(e.begin);

            for (int i = 0; i < e.pending_count; i++) {
                MaxOutPendingWrite *pw = &e.pending[i];
                if (pw->slot_idx < 0 || pw->slot_idx >= MAX_SLOTS) continue;
                MaxOutSlot *s = &e.slots[pw->slot_idx];
                if (s->state != SLOT_QUEUED_WRITE || s->gen != pw->gen) continue;

                if (!pw->is_like) {
                    sqlite3_reset(e.insert);
                    sqlite3_bind_text(e.insert, 1, pw->author, -1, SQLITE_STATIC);
                    sqlite3_bind_text(e.insert, 2, pw->content, -1, SQLITE_STATIC);
                    sqlite3_bind_int64(e.insert, 3, pw->created_at);
                    sqlite3_step(e.insert);

                    int64_t new_id = sqlite3_last_insert_rowid(e.db);
                    char body[64];
                    char *bp = body;
                    memcpy(bp, "{\"id\":", 6); bp += 6;
                    bp = fast_i64toa(bp, new_id);
                    memcpy(bp, ",\"status\":\"created\"}", 20); bp += 20;
                    make_response(s, body, (size_t)(bp - body), 1);
                } else {
                    sqlite3_reset(e.like);
                    sqlite3_bind_int64(e.like, 1, pw->id);
                    sqlite3_step(e.like);

                    char body[64];
                    char *bp = body;
                    memcpy(bp, "{\"id\":", 6); bp += 6;
                    bp = fast_i64toa(bp, pw->id);
                    memcpy(bp, ",\"status\":\"liked\"}", 18); bp += 18;
                    make_response(s, body, (size_t)(bp - body), 1);
                }
                arm_send(&e, pw->slot_idx);
            }

            sqlite3_step(e.commit);
            sqlite3_reset(e.commit);
            e.pending_count = 0;
            e.feed_valid = 0;
        }
    }

    return hp_of_int(0);
}
#else
hval hpbi_holy_maxout_start(hval port_val, hval db_path_val) {
    (void)port_val; (void)db_path_val;
    hp_throw_str("holy_maxout_start() requires Linux io_uring");
    return hp_of_int(-1);
}
#endif
