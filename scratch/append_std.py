# append_std.py
code = r"""
/* ================= HolyPHP MaxOut io_uring Engine ================= */
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
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>

#define MAX_SLOTS 2048
#define BUF_SIZE 4096
#define WRITE_BUF_SIZE 16384
#define BATCH_SIZE 64
#define RING_ENTRIES 1024

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

typedef struct {
    int fd;
    int state;
    int keep_alive;
    size_t read_len;
    size_t write_offset;
    size_t write_total;
    char read_buf[BUF_SIZE];
    char write_buf[WRITE_BUF_SIZE];
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
    while (v > 0) {
        tmp[i++] = (v % 10) + '0';
        v /= 10;
    }
    while (i > 0) {
        *p++ = tmp[--i];
    }
    return p;
}

static inline char *fast_i64toa(char *p, int64_t v) {
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
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
        else { *p++ = c; }
    }
    *p++ = '"';
    *dst = p;
}

static inline void make_http_response_fast(MaxOutSlot *s, const char *body, size_t body_len, int is_json) {
    char *p = s->write_buf;
    memcpy(p, "HTTP/1.1 200 OK\r\nServer: HolyPHP-MaxOut\r\nContent-Type: ", 50);
    p += 50;
    if (is_json) {
        memcpy(p, "application/json\r\n", 18);
        p += 18;
    } else {
        memcpy(p, "text/plain\r\n", 12);
        p += 12;
    }
    memcpy(p, "Content-Length: ", 16);
    p += 16;
    p = fast_u64toa(p, body_len);
    if (s->keep_alive) {
        memcpy(p, "\r\nConnection: keep-alive\r\n\r\n", 28);
        p += 28;
    } else {
        memcpy(p, "\r\nConnection: close\r\n\r\n", 23);
        p += 23;
    }
    memcpy(p, body, body_len);
    p += body_len;
    s->write_offset = 0;
    s->write_total = p - s->write_buf;
    s->state = SLOT_WRITING;
}

hval hpbi_holy_maxout_start(hval port_val, hval db_path_val) {
    int port = (int)hp_val_to_int(port_val);
    if (port <= 0) port = 8080;
    hstr *ds = hp_val_to_str(db_path_val);
    const char *db_path = ds && ds->len ? ds->data : "benchmark.db";

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "HolyPHP: Failed to open SQLite database: %s\n", sqlite3_errmsg(db));
        return hp_of_int(-1);
    }

    char *errmsg = NULL;
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", NULL, NULL, &errmsg);
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", NULL, NULL, &errmsg);
    sqlite3_exec(db, "PRAGMA cache_size = -64000;", NULL, NULL, &errmsg);
    sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", NULL, NULL, &errmsg);
    sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", NULL, NULL, &errmsg);
    sqlite3_exec(db, "PRAGMA busy_timeout = 5000;", NULL, NULL, &errmsg);

    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS posts ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "author TEXT NOT NULL,"
                     "content TEXT NOT NULL,"
                     "likes INTEGER NOT NULL DEFAULT 0,"
                     "created_at INTEGER NOT NULL);", NULL, NULL, &errmsg);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_posts_id ON posts(id DESC);", NULL, NULL, &errmsg);

    sqlite3_exec(db, "INSERT INTO posts (author, content, likes, created_at) "
                     "SELECT 'benchmark_user', 'Initial benchmark post content', 10, 1698500000 "
                     "WHERE NOT EXISTS (SELECT 1 FROM posts LIMIT 1);", NULL, NULL, &errmsg);

    sqlite3_stmt *stmt_feed = NULL, *stmt_insert = NULL, *stmt_like = NULL;
    sqlite3_stmt *stmt_begin = NULL, *stmt_commit = NULL;

    sqlite3_prepare_v2(db, "SELECT id, author, content, likes, created_at FROM posts ORDER BY id DESC LIMIT ?;", -1, &stmt_feed, NULL);
    sqlite3_prepare_v2(db, "INSERT INTO posts(author, content, likes, created_at) VALUES(?, ?, 0, ?);", -1, &stmt_insert, NULL);
    sqlite3_prepare_v2(db, "UPDATE posts SET likes = likes + 1 WHERE id = ?;", -1, &stmt_like, NULL);
    sqlite3_prepare_v2(db, "BEGIN IMMEDIATE;", -1, &stmt_begin, NULL);
    sqlite3_prepare_v2(db, "COMMIT;", -1, &stmt_commit, NULL);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        perror("HolyPHP: bind failed");
        return hp_of_int(-1);
    }
    if (listen(listen_fd, 4096) != 0) {
        perror("HolyPHP: listen failed");
        return hp_of_int(-1);
    }

    struct io_uring ring;
    if (io_uring_queue_init(RING_ENTRIES, &ring, 0) != 0) {
        perror("HolyPHP: io_uring_queue_init failed");
        return hp_of_int(-1);
    }

    MaxOutSlot *slots = (MaxOutSlot *)calloc(MAX_SLOTS, sizeof(MaxOutSlot));
    int *free_slots = (int *)malloc(MAX_SLOTS * sizeof(int));
    int free_slot_count = MAX_SLOTS;
    for (int i = 0; i < MAX_SLOTS; i++) {
        slots[i].fd = -1;
        free_slots[i] = MAX_SLOTS - 1 - i;
    }

    MaxOutPendingWrite pending_writes[BATCH_SIZE * 2];
    int pending_count = 0;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
    io_uring_sqe_set_data64(sqe, OP_ACCEPT);
    io_uring_submit(&ring);

    char json_scratch[8192];

    for (;;) {
        struct io_uring_cqe *cqe;
        int ret = io_uring_submit_and_wait(&ring, 1);
        if (ret < 0 && ret != -EINTR) break;

        unsigned head;
        int count = 0;
        int rearm_accept = 0;

        io_uring_for_each_cqe(&ring, head, cqe) {
            count++;
            uint64_t user_data = io_uring_cqe_get_data64(cqe);
            int res = cqe->res;

            if (user_data == OP_ACCEPT) {
                rearm_accept = 1;
                if (res >= 0) {
                    int client_fd = res;
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                    if (free_slot_count > 0) {
                        int slot_idx = free_slots[--free_slot_count];
                        MaxOutSlot *s = &slots[slot_idx];
                        s->fd = client_fd;
                        s->state = SLOT_READING;
                        s->read_len = 0;
                        s->write_offset = 0;
                        s->write_total = 0;
                        s->keep_alive = 1;

                        struct io_uring_sqe *r_sqe = io_uring_get_sqe(&ring);
                        if (r_sqe) {
                            io_uring_prep_recv(r_sqe, client_fd, s->read_buf, BUF_SIZE, 0);
                            io_uring_sqe_set_data64(r_sqe, ((uint64_t)slot_idx << 2) | OP_RECV);
                        }
                    } else {
                        close(client_fd);
                    }
                }
            } else {
                uint32_t op = user_data & 0x03;
                int slot_idx = (int)(user_data >> 2);

                if (slot_idx < MAX_SLOTS) {
                    MaxOutSlot *s = &slots[slot_idx];
                    if (op == OP_RECV) {
                        if (res <= 0) {
                            if (s->fd >= 0) { close(s->fd); s->fd = -1; }
                            s->state = SLOT_FREE;
                            free_slots[free_slot_count++] = slot_idx;
                        } else {
                            s->read_len = (size_t)res;
                            const char *req = s->read_buf;

                            if (memmem(req, s->read_len, "close", 5)) {
                                s->keep_alive = 0;
                            }

                            if (s->read_len >= 11 && memcmp(req, "GET /health", 11) == 0) {
                                make_http_response_fast(s, "{\"status\":\"ok\"}", 15, 1);
                                struct io_uring_sqe *w_sqe = io_uring_get_sqe(&ring);
                                if (w_sqe) {
                                    io_uring_prep_send(w_sqe, s->fd, s->write_buf, s->write_total, 0);
                                    io_uring_sqe_set_data64(w_sqe, ((uint64_t)slot_idx << 2) | OP_SEND);
                                }
                            } else if ((s->read_len >= 9 && memcmp(req, "GET /feed", 9) == 0) ||
                                       (s->read_len >= 10 && memcmp(req, "GET /posts", 10) == 0)) {
                                sqlite3_reset(stmt_feed);
                                sqlite3_bind_int(stmt_feed, 1, 20);

                                char *jp = json_scratch;
                                *jp++ = '[';
                                int first = 1;

                                while (sqlite3_step(stmt_feed) == SQLITE_ROW) {
                                    if (!first) *jp++ = ',';
                                    first = 0;

                                    int64_t id = sqlite3_column_int64(stmt_feed, 0);
                                    const char *author = (const char *)sqlite3_column_text(stmt_feed, 1);
                                    int author_len = sqlite3_column_bytes(stmt_feed, 1);
                                    const char *content = (const char *)sqlite3_column_text(stmt_feed, 2);
                                    int content_len = sqlite3_column_bytes(stmt_feed, 2);
                                    int64_t likes = sqlite3_column_int64(stmt_feed, 3);
                                    int64_t created_at = sqlite3_column_int64(stmt_feed, 4);

                                    memcpy(jp, "{\"id\":", 6); jp += 6;
                                    jp = fast_i64toa(jp, id);
                                    memcpy(jp, ",\"author\":", 10); jp += 10;
                                    escape_and_write_json(&jp, author ? author : "", author_len);
                                    memcpy(jp, ",\"content\":", 11); jp += 11;
                                    escape_and_write_json(&jp, content ? content : "", content_len);
                                    memcpy(jp, ",\"likes\":", 9); jp += 9;
                                    jp = fast_i64toa(jp, likes);
                                    memcpy(jp, ",\"created_at\":", 14); jp += 14;
                                    jp = fast_i64toa(jp, created_at);
                                    *jp++ = '}';
                                }
                                *jp++ = ']';

                                make_http_response_fast(s, json_scratch, jp - json_scratch, 1);
                                struct io_uring_sqe *w_sqe = io_uring_get_sqe(&ring);
                                if (w_sqe) {
                                    io_uring_prep_send(w_sqe, s->fd, s->write_buf, s->write_total, 0);
                                    io_uring_sqe_set_data64(w_sqe, ((uint64_t)slot_idx << 2) | OP_SEND);
                                }
                            } else if (s->read_len >= 11 && memcmp(req, "POST /posts", 11) == 0) {
                                if (pending_count < BATCH_SIZE * 2) {
                                    MaxOutPendingWrite *pw = &pending_writes[pending_count++];
                                    pw->slot_idx = slot_idx;
                                    pw->is_like = 0;
                                    pw->created_at = (int64_t)time(NULL);
                                    strcpy(pw->author, "anonymous");
                                    strcpy(pw->content, "hello world");

                                    const char *bstart = strstr(req, "\r\n\r\n");
                                    if (bstart) {
                                        bstart += 4;
                                        const char *a_pos = strstr(bstart, "\"author\":\"");
                                        if (a_pos) {
                                            a_pos += 10;
                                            const char *a_end = strchr(a_pos, '"');
                                            if (a_end) {
                                                size_t alen = a_end - a_pos;
                                                if (alen >= sizeof(pw->author)) alen = sizeof(pw->author) - 1;
                                                memcpy(pw->author, a_pos, alen);
                                                pw->author[alen] = '\0';
                                            }
                                        }
                                        const char *c_pos = strstr(bstart, "\"content\":\"");
                                        if (c_pos) {
                                            c_pos += 11;
                                            const char *c_end = strchr(c_pos, '"');
                                            if (c_end) {
                                                size_t clen = c_end - c_pos;
                                                if (clen >= sizeof(pw->content)) clen = sizeof(pw->content) - 1;
                                                memcpy(pw->content, c_pos, clen);
                                                pw->content[clen] = '\0';
                                            }
                                        }
                                    }
                                    s->state = SLOT_QUEUED_WRITE;
                                }
                            } else if (s->read_len >= 10 && memcmp(req, "POST /like", 10) == 0) {
                                if (pending_count < BATCH_SIZE * 2) {
                                    MaxOutPendingWrite *pw = &pending_writes[pending_count++];
                                    pw->slot_idx = slot_idx;
                                    pw->is_like = 1;
                                    pw->id = 1;

                                    const char *id_pos = strstr(req, "?id=");
                                    if (id_pos) {
                                        pw->id = atoll(id_pos + 4);
                                        if (pw->id <= 0) pw->id = 1;
                                    }
                                    s->state = SLOT_QUEUED_WRITE;
                                }
                            } else {
                                const char not_found[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                                memcpy(s->write_buf, not_found, sizeof(not_found) - 1);
                                s->write_offset = 0;
                                s->write_total = sizeof(not_found) - 1;
                                s->state = SLOT_WRITING;
                                struct io_uring_sqe *w_sqe = io_uring_get_sqe(&ring);
                                if (w_sqe) {
                                    io_uring_prep_send(w_sqe, s->fd, s->write_buf, s->write_total, 0);
                                    io_uring_sqe_set_data64(w_sqe, ((uint64_t)slot_idx << 2) | OP_SEND);
                                }
                            }
                        }
                    } else if (op == OP_SEND) {
                        if (res <= 0) {
                            if (s->fd >= 0) { close(s->fd); s->fd = -1; }
                            s->state = SLOT_FREE;
                            free_slots[free_slot_count++] = slot_idx;
                        } else {
                            s->write_offset += (size_t)res;
                            if (s->write_offset < s->write_total) {
                                struct io_uring_sqe *w_sqe = io_uring_get_sqe(&ring);
                                if (w_sqe) {
                                    io_uring_prep_send(w_sqe, s->fd, s->write_buf + s->write_offset, s->write_total - s->write_offset, 0);
                                    io_uring_sqe_set_data64(w_sqe, ((uint64_t)slot_idx << 2) | OP_SEND);
                                }
                            } else {
                                if (s->keep_alive) {
                                    s->state = SLOT_READING;
                                    s->read_len = 0;
                                    s->write_offset = 0;
                                    s->write_total = 0;
                                    struct io_uring_sqe *r_sqe = io_uring_get_sqe(&ring);
                                    if (r_sqe) {
                                        io_uring_prep_recv(r_sqe, s->fd, s->read_buf, BUF_SIZE, 0);
                                        io_uring_sqe_set_data64(r_sqe, ((uint64_t)slot_idx << 2) | OP_RECV);
                                    }
                                } else {
                                    if (s->fd >= 0) { close(s->fd); s->fd = -1; }
                                    s->state = SLOT_FREE;
                                    free_slots[free_slot_count++] = slot_idx;
                                }
                            }
                        }
                    }
                }
            }
        }

        io_uring_cq_advance(&ring, count);

        if (rearm_accept) {
            struct io_uring_sqe *a_sqe = io_uring_get_sqe(&ring);
            if (a_sqe) {
                io_uring_prep_accept(a_sqe, listen_fd, NULL, NULL, 0);
                io_uring_sqe_set_data64(a_sqe, OP_ACCEPT);
            }
        }

        if (pending_count > 0) {
            sqlite3_step(stmt_begin);
            sqlite3_reset(stmt_begin);

            for (int i = 0; i < pending_count; i++) {
                MaxOutPendingWrite *pw = &pending_writes[i];
                MaxOutSlot *s = &slots[pw->slot_idx];

                if (!pw->is_like) {
                    sqlite3_reset(stmt_insert);
                    sqlite3_bind_text(stmt_insert, 1, pw->author, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt_insert, 2, pw->content, -1, SQLITE_STATIC);
                    sqlite3_bind_int64(stmt_insert, 3, pw->created_at);
                    sqlite3_step(stmt_insert);

                    int64_t new_id = sqlite3_last_insert_rowid(db);
                    char res_body[64];
                    char *bp = res_body;
                    memcpy(bp, "{\"id\":", 6); bp += 6;
                    bp = fast_i64toa(bp, new_id);
                    memcpy(bp, ",\"status\":\"created\"}", 20); bp += 20;

                    make_http_response_fast(s, res_body, bp - res_body, 1);
                } else {
                    sqlite3_reset(stmt_like);
                    sqlite3_bind_int64(stmt_like, 1, pw->id);
                    sqlite3_step(stmt_like);

                    char res_body[64];
                    char *bp = res_body;
                    memcpy(bp, "{\"id\":", 6); bp += 6;
                    bp = fast_i64toa(bp, pw->id);
                    memcpy(bp, ",\"status\":\"liked\"}", 18); bp += 18;

                    make_http_response_fast(s, res_body, bp - res_body, 1);
                }

                struct io_uring_sqe *w_sqe = io_uring_get_sqe(&ring);
                if (w_sqe) {
                    io_uring_prep_send(w_sqe, s->fd, s->write_buf, s->write_total, 0);
                    io_uring_sqe_set_data64(w_sqe, ((uint64_t)pw->slot_idx << 2) | OP_SEND);
                }
            }

            sqlite3_step(stmt_commit);
            sqlite3_reset(stmt_commit);
            pending_count = 0;
        }

        io_uring_submit(&ring);
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
"""

with open('src/hphpc/runtime/hphp_std.c', 'r', encoding='utf-8') as f:
    orig = f.read()

if 'hpbi_holy_maxout_start' not in orig:
    orig += code
    with open('src/hphpc/runtime/hphp_std.c', 'w', encoding='utf-8', newline='\n') as f:
        f.write(orig)
    print('Appended holy_maxout_start to hphp_std.c')
