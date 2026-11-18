/* feedbench.c - isolate the SQL cost of the two feed strategies.
 *   cc -O2 -o feedbench feedbench.c -lsqlite3 && ./feedbench /root/challenge/seed/feed.db
 */
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

#define LIMIT 20

/* A: one query, join + correlated count per row (current HolyPHP plan) */
static long run_a(sqlite3 *db, int iters, unsigned char *sink) {
    static const char *sql =
        "SELECT p.id, p.body, p.created_at, u.username,"
        " (SELECT count(*) FROM likes l WHERE l.post_id = p.id)"
        " FROM posts p JOIN users u ON u.id = p.user_id"
        " ORDER BY p.created_at DESC, p.id DESC LIMIT 20;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) { printf("A prepare: %s\n", sqlite3_errmsg(db)); return -1; }
    long n = 0;
    for (int i = 0; i < iters; i++) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *b = sqlite3_column_text(st, 1);
            int bl = sqlite3_column_bytes(st, 1);
            const unsigned char *u = sqlite3_column_text(st, 3);
            int ul = sqlite3_column_bytes(st, 3);
            sqlite3_column_int64(st, 0); sqlite3_column_int64(st, 4);
            memcpy(sink, b, (size_t)bl); memcpy(sink + bl, u, (size_t)ul);
            n++;
        }
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    return n;
}

/* B: id scan + batched like counts + batched usernames (new plan) */
static long run_b(sqlite3 *db, int iters, unsigned char *sink) {
    static const char *sq =
        "SELECT id, user_id, body, created_at FROM posts"
        " ORDER BY created_at DESC, id DESC LIMIT 20;";
    static const char *sc =
        "SELECT post_id, count(*) FROM likes WHERE post_id BETWEEN ?1 AND ?2 GROUP BY post_id;";
    static const char *su =
        "SELECT id, username FROM users WHERE id IN (%s)";
    char ubuf[512];
    { char *w = ubuf + sprintf(ubuf, "SELECT id, username FROM users WHERE id IN (");
      for (int i = 0; i < LIMIT; i++) w += sprintf(w, i ? ",?" : "?");
      sprintf(w, ");"); }
    (void)su;

    sqlite3_stmt *s1 = NULL, *s2 = NULL, *s3 = NULL;
    if (sqlite3_prepare_v2(db, sq, -1, &s1, NULL) != SQLITE_OK) { printf("B s1: %s\n", sqlite3_errmsg(db)); return -1; }
    if (sqlite3_prepare_v2(db, sc, -1, &s2, NULL) != SQLITE_OK) { printf("B s2: %s\n", sqlite3_errmsg(db)); return -1; }
    if (sqlite3_prepare_v2(db, ubuf, -1, &s3, NULL) != SQLITE_OK) { printf("B s3: %s\n", sqlite3_errmsg(db)); return -1; }

    int64_t ids[LIMIT], uids[LIMIT];
    static char bodybuf[LIMIT][1024]; static int bodyl[LIMIT];
    static char timebuf[LIMIT][64]; static int ctimel[LIMIT];
    static char namebuf[LIMIT][64]; static int namel[LIMIT];
    int64_t counts[LIMIT];

    long n = 0;
    for (int i = 0; i < iters; i++) {
        int k = 0; int64_t lo = 0, hi = 0;
        while (k < LIMIT && sqlite3_step(s1) == SQLITE_ROW) {
            ids[k] = sqlite3_column_int64(s1, 0);
            uids[k] = sqlite3_column_int64(s1, 1);
            const unsigned char *b = sqlite3_column_text(s1, 2);
            int bl = sqlite3_column_bytes(s1, 2); if (bl > 1023) bl = 1023;
            memcpy(bodybuf[k], b ? (const char *)b : "", (size_t)bl); bodyl[k] = bl;
            const unsigned char *c = sqlite3_column_text(s1, 3);
            int cl = sqlite3_column_bytes(s1, 3); if (cl > 63) cl = 63;
            memcpy(timebuf[k], c ? (const char *)c : "", (size_t)cl); ctimel[k] = cl;
            if (k == 0) lo = hi = ids[k];
            else { if (ids[k] < lo) lo = ids[k]; if (ids[k] > hi) hi = ids[k]; }
            k++;
        }
        sqlite3_reset(s1);
        for (int j = 0; j < k; j++) counts[j] = 0;
        sqlite3_reset(s2); sqlite3_bind_int64(s2, 1, lo); sqlite3_bind_int64(s2, 2, hi);
        while (sqlite3_step(s2) == SQLITE_ROW) {
            int64_t pid = sqlite3_column_int64(s2, 0), c = sqlite3_column_int64(s2, 1);
            for (int j = 0; j < k; j++) if (ids[j] == pid) { counts[j] = c; break; }
        }
        sqlite3_reset(s2);
        sqlite3_reset(s3);
        for (int j = 0; j < k; j++) { sqlite3_bind_int64(s3, j + 1, uids[j]); namel[j] = 0; }
        while (sqlite3_step(s3) == SQLITE_ROW) {
            int64_t uid = sqlite3_column_int64(s3, 0);
            const unsigned char *nm = sqlite3_column_text(s3, 1);
            int nl = sqlite3_column_bytes(s3, 1); if (nl > 63) nl = 63;
            for (int j = 0; j < k; j++) if (uids[j] == uid) { memcpy(namebuf[j], nm ? (const char *)nm : "", (size_t)nl); namel[j] = nl; break; }
        }
        for (int j = 0; j < k; j++) {
            memcpy(sink, bodybuf[j], (size_t)bodyl[j]); sink += bodyl[j];
            memcpy(sink, namebuf[j], (size_t)namel[j]); sink += namel[j];
            memcpy(sink, timebuf[j], (size_t)ctimel[j]); sink += ctimel[j];
            n++;
        }
    }
    sqlite3_finalize(s1); sqlite3_finalize(s2); sqlite3_finalize(s3);
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s db\n", argv[0]); return 1; }
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) { fprintf(stderr, "%s\n", sqlite3_errmsg(db)); return 1; }
    sqlite3_exec(db, "PRAGMA mmap_size=1073741824;", NULL, NULL, NULL);
    unsigned char *sink = malloc(64 << 20);   /* 64 MiB to absorb every variant */
    int iters = argc > 2 ? atoi(argv[2]) : 2000;

    long n = run_a(db, 100, sink); (void)n;             /* warmup */
    double t0 = now_ns(); n = run_a(db, iters, sink); double a = now_ns() - t0;
    t0 = now_ns(); n = run_b(db, iters, sink); double b = now_ns() - t0;
    t0 = now_ns(); n = run_b(db, iters, sink); b = now_ns() - t0;
    t0 = now_ns(); n = run_a(db, iters, sink); a = now_ns() - t0;

    printf("rows=%ld iters=%d\n", n, iters);
    printf("A (join + correlated count) : %8.1f us/feed\n", a / iters / 1000.0);
    printf("B (id scan + batched counts): %8.1f us/feed\n", b / iters / 1000.0);
    printf("delta: %+.1f%%\n", 100.0 * (b - a) / a);
    sqlite3_close(db);
    free(sink);
    return 0;
}
