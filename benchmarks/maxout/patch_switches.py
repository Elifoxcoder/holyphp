import io, re

p = 'benchmarks/maxout/engine_v2.c'
s = io.open(p, encoding='utf-8').read()

# 1) per-slot receive buffer for the single-shot fallback
s = s.replace(
    "    size_t resp_off;\n    char req_buf[REQ_BUF_SIZE];",
    "    size_t resp_off;\n    char recv_buf[RECV_BUF_SIZE];\n    char req_buf[REQ_BUF_SIZE];",
)

# 2) runtime switches
s = s.replace(
    "typedef struct {\n    sqlite3 *db;",
    "static int g_multishot_recv = 1;   /* HPHP_MR=0 -> plain single-shot recv */\n"
    "static int g_feed_cache = 1;       /* HPHP_CACHE=0 -> always rebuild feed */\n\n"
    "typedef struct {\n    sqlite3 *db;",
)

# 3) arm_recv chooses the receive strategy
old_arm = """static void arm_recv(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (!sqe) return;
    io_uring_prep_recv_multishot(sqe, s->fd, NULL, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = BGID;
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_RECV));
}"""
new_arm = """static void arm_recv(MaxOutEngine *e, int idx) {
    MaxOutSlot *s = &e->slots[idx];
    struct io_uring_sqe *sqe = io_uring_get_sqe(e->ring);
    if (!sqe) return;
    if (g_multishot_recv) {
        io_uring_prep_recv_multishot(sqe, s->fd, NULL, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = BGID;
    } else {
        io_uring_prep_recv(sqe, s->fd, s->recv_buf, RECV_BUF_SIZE, 0);
    }
    io_uring_sqe_set_data64(sqe, TAG(idx, OP_RECV));
}"""
assert s.count(old_arm) == 1
s = s.replace(old_arm, new_arm)

# 4) feed cache gated on the switch
s = s.replace("static void build_feed(MaxOutEngine *e) {\n    if (e->feed_valid) return;",
              "static void build_feed(MaxOutEngine *e) {\n    if (e->feed_valid) return;")

# 5) recv completion: buffer may be provided or the slot's own
old_recv = """                if (op == OP_RECV) {
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
                        int bad = 0;
                        if (had_buf) {
                            bad = feed_bytes(&e, idx,
                                             e.buf_base + (size_t)bid * RECV_BUF_SIZE,
                                             (size_t)res) < 0;
                            return_buffer(&e, bid);
                        }
                        if (bad) {
                            release_slot(&e, idx);
                        } else if (!(cflags & IORING_CQE_F_MORE) &&
                                   e.slots[idx].state != SLOT_FREE) {
                            arm_recv(&e, idx);
                        }
                    }
"""
new_recv = """                if (op == OP_RECV) {
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
"""
assert s.count(old_recv) == 1
s = s.replace(old_recv, new_recv)

# 6) read switches from the environment before the ring is built
old_ring = """    /* ---- io_uring ---- */
    struct io_uring ring;
    int rc = io_uring_queue_init(RING_ENTRIES, &ring,
                                 IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN);
    if (rc < 0)
        rc = io_uring_queue_init(RING_ENTRIES, &ring,
                                 IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN);
    if (rc < 0)
        rc = io_uring_queue_init(RING_ENTRIES, &ring, 0);
"""
new_ring = """    /* ---- io_uring ---- */
    const char *mr = getenv("HPHP_MR");
    if (mr && *mr == '0') g_multishot_recv = 0;
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
"""
assert s.count(old_ring) == 1
s = s.replace(old_ring, new_ring)

# 7) gate the cache
s = s.replace("static void build_feed(MaxOutEngine *e) {\n    if (e->feed_valid) return;",
              "static void build_feed(MaxOutEngine *e) {\n    if (g_feed_cache && e->feed_valid) return;")

# avoid unused warnings when the cache is disabled
s = s.replace("    e->feed_len = (size_t)(jp - e->feed_json);\n    e->feed_valid = 1;",
              "    e->feed_len = (size_t)(jp - e->feed_json);\n    e->feed_valid = 1;")

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('patched')
