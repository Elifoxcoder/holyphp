import io

p = 'benchmarks/maxout/engine_v2.c'
s = io.open(p, encoding='utf-8').read()

# per-slot response source pointer
s = s.replace(
    "    size_t resp_len;\n    size_t resp_off;",
    "    size_t resp_len;\n    size_t resp_off;\n    const char *resp_ptr;",
)

# engine-held pre-assembled feed response
s = s.replace(
    "    char feed_json[FEED_CACHE_SIZE];\n    size_t feed_len;\n    int feed_valid;",
    "    char feed_json[FEED_CACHE_SIZE];\n    size_t feed_len;\n"
    "    char feed_http[FEED_CACHE_SIZE + 512];\n    size_t feed_http_len;\n    int feed_valid;",
)

s = s.replace(
    "    memcpy(p, body, body_len);\n    p += body_len;\n    s->resp_off = 0;\n    s->resp_len = (size_t)(p - s->resp_buf);",
    "    memcpy(p, body, body_len);\n    p += body_len;\n    s->resp_off = 0;\n    s->resp_ptr = s->resp_buf;\n    s->resp_len = (size_t)(p - s->resp_buf);",
)

s = s.replace(
    "    memcpy(s->resp_buf, nf, sizeof nf - 1);\n    s->resp_off = 0;\n    s->resp_len = sizeof nf - 1;",
    "    memcpy(s->resp_buf, nf, sizeof nf - 1);\n    s->resp_off = 0;\n    s->resp_ptr = s->resp_buf;\n    s->resp_len = sizeof nf - 1;",
)

s = s.replace(
    "    io_uring_prep_send(sqe, s->fd, s->resp_buf + s->resp_off,",
    "    io_uring_prep_send(sqe, s->fd, s->resp_ptr + s->resp_off,",
)

# build the pre-assembled wire response together with the JSON body
s = s.replace(
    "    e->feed_len = (size_t)(jp - e->feed_json);\n    e->feed_valid = 1;",
    """    e->feed_len = (size_t)(jp - e->feed_json);
    {
        /* Pre-assemble the whole wire response once: subsequent feed hits
         * send these bytes directly, with no header formatting and no copy
         * of the body. Deterministic, so Content-Length is baked in. */
        static MaxOutSlot tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.keep_alive = 1;
        make_response(&tmp, e->feed_json, e->feed_len, 1);
        memcpy(e->feed_http, tmp.resp_buf, tmp.resp_len);
        e->feed_http_len = tmp.resp_len;
    }
    e->feed_valid = 1;""",
)

# feed path: send the pre-assembled response for keep-alive requests
s = s.replace(
    """        build_feed(e);
        make_response(s, e->feed_json, e->feed_len, 1);
        return 1;""",
    """        build_feed(e);
        if (s->keep_alive && e->feed_http_len) {
            s->resp_ptr = e->feed_http;
            s->resp_off = 0;
            s->resp_len = e->feed_http_len;
            s->state = SLOT_WRITING;
        } else {
            make_response(s, e->feed_json, e->feed_len, 1);
        }
        return 1;""",
)

# slot init after accept
s = s.replace(
    "                        s->resp_len = 0;\n                        s->resp_off = 0;\n                        arm_recv(&e, si);",
    "                        s->resp_len = 0;\n                        s->resp_off = 0;\n                        s->resp_ptr = s->resp_buf;\n                        arm_recv(&e, si);",
)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('preassembled feed patched')
