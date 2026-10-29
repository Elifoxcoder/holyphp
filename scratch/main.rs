// main.rs — MaxOut reference server in Rust: raw io_uring + SQLite WAL.
//
// Design mirrors the HolyPHP engine (../src/hphpc/runtime/hphp_std.c):
//   * one thread, one io_uring, no async runtime
//   * multi-shot accept: one SQE keeps accepting
//   * single-shot recv per connection, re-armed after each request
//   * prepared SQLite statements, WAL + synchronous=NORMAL
//   * group commit: every write in one CQE batch shares BEGIN..COMMIT
//   * response bytes are rendered in place into a per-connection buffer
//   * feed body cache, invalidated by writes (no per-request allocation)
//
// Build: cargo build --release   (io-uring, libc, libsqlite3-sys)

use io_uring::{cqueue, opcode, types, IoUring};
use std::ffi::CString;
use std::os::unix::io::AsRawFd;
use std::ptr;
use std::slice;
use std::time::{SystemTime, UNIX_EPOCH};

const MAX_CONNECTIONS: usize = 2048;
const BUF_SIZE: usize = 4096;
const WRITE_BUF_SIZE: usize = 16384;
const RING_ENTRIES: u32 = 4096;
const FEED_LIMIT: i32 = 20;
const BATCH_MAX: usize = 4096;

const OP_ACCEPT: u64 = 0;
const OP_RECV: u64 = 1;
const OP_SEND: u64 = 2;

#[derive(Clone, Copy, PartialEq, Eq)]
enum SlotState {
    Free,
    Reading,
    Writing,
    QueuedWrite,
}

struct Slot {
    fd: i32,
    state: SlotState,
    keep_alive: bool,
    seq: u32,
    req_len: usize,
    write_len: usize,
    write_off: usize,
    recv_buf: [u8; BUF_SIZE],
    req_buf: [u8; BUF_SIZE],
    write_buf: [u8; WRITE_BUF_SIZE],
}

impl Slot {
    fn new() -> Self {
        Self {
            fd: -1,
            state: SlotState::Free,
            keep_alive: true,
            seq: 0,
            req_len: 0,
            write_len: 0,
            write_off: 0,
            recv_buf: [0u8; BUF_SIZE],
            req_buf: [0u8; BUF_SIZE],
            write_buf: [0u8; WRITE_BUF_SIZE],
        }
    }
}

enum WriteKind {
    Insert { author: [u8; 64], author_len: usize, content: [u8; 256], content_len: usize, created_at: i64 },
    Like { id: i64 },
}

struct PendingWrite {
    slot_idx: usize,
    seq: u32,
    kind: WriteKind,
}

struct Db {
    db: *mut libsqlite3_sys::sqlite3,
    stmt_feed: *mut libsqlite3_sys::sqlite3_stmt,
    stmt_insert: *mut libsqlite3_sys::sqlite3_stmt,
    stmt_like: *mut libsqlite3_sys::sqlite3_stmt,
    stmt_begin: *mut libsqlite3_sys::sqlite3_stmt,
    stmt_commit: *mut libsqlite3_sys::sqlite3_stmt,
    feed_json: Vec<u8>,
    feed_valid: bool,
}

impl Db {
    fn open(path: &str) -> Self {
        unsafe {
            let c_path = CString::new(path).unwrap();
            let mut db: *mut libsqlite3_sys::sqlite3 = ptr::null_mut();
            let flags = libsqlite3_sys::SQLITE_OPEN_READWRITE
                | libsqlite3_sys::SQLITE_OPEN_CREATE
                | libsqlite3_sys::SQLITE_OPEN_NOMUTEX;

            let rc = libsqlite3_sys::sqlite3_open_v2(c_path.as_ptr(), &mut db, flags, ptr::null());
            if rc != libsqlite3_sys::SQLITE_OK {
                panic!("Cannot open SQLite db: {}", rc);
            }

            let exec = |sql: &str| {
                let c_sql = CString::new(sql).unwrap();
                let mut errmsg = ptr::null_mut();
                libsqlite3_sys::sqlite3_exec(db, c_sql.as_ptr(), None, ptr::null_mut(), &mut errmsg);
            };

            exec("PRAGMA journal_mode = WAL;");
            exec("PRAGMA synchronous = NORMAL;");
            exec("PRAGMA cache_size = -64000;");
            exec("PRAGMA temp_store = MEMORY;");
            exec("PRAGMA mmap_size = 268435456;");
            exec("PRAGMA busy_timeout = 5000;");
            exec("PRAGMA wal_autocheckpoint = 4096;");

            exec(
                "CREATE TABLE IF NOT EXISTS posts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    author TEXT NOT NULL,
                    content TEXT NOT NULL,
                    likes INTEGER NOT NULL DEFAULT 0,
                    created_at INTEGER NOT NULL
                );",
            );

            // Seed one row on an empty database, same as the HolyPHP engine.
            exec(
                "INSERT INTO posts (author, content, likes, created_at)
                 SELECT 'benchmark_user', 'Initial benchmark post content', 10, 1698500000
                 WHERE NOT EXISTS (SELECT 1 FROM posts LIMIT 1);",
            );

            let prepare = |sql: &str| -> *mut libsqlite3_sys::sqlite3_stmt {
                let c_sql = CString::new(sql).unwrap();
                let mut stmt = ptr::null_mut();
                libsqlite3_sys::sqlite3_prepare_v2(db, c_sql.as_ptr(), -1, &mut stmt, ptr::null_mut());
                stmt
            };

            let stmt_feed = prepare("SELECT id, author, content, likes, created_at FROM posts ORDER BY id DESC LIMIT ?;");
            let stmt_insert = prepare("INSERT INTO posts(author, content, likes, created_at) VALUES(?, ?, 0, ?);");
            let stmt_like = prepare("UPDATE posts SET likes = likes + 1 WHERE id = ?;");
            let stmt_begin = prepare("BEGIN IMMEDIATE;");
            let stmt_commit = prepare("COMMIT;");

            Self {
                db,
                stmt_feed,
                stmt_insert,
                stmt_like,
                stmt_begin,
                stmt_commit,
                feed_json: Vec::with_capacity(8192),
                feed_valid: false,
            }
        }
    }

    /// Render the latest FEED_LIMIT posts as JSON into feed_json.
    fn build_feed(&mut self) {
        if self.feed_valid {
            return;
        }
        self.feed_json.clear();
        self.feed_json.push(b'[');
        let mut first = true;
        unsafe {
            libsqlite3_sys::sqlite3_reset(self.stmt_feed);
            libsqlite3_sys::sqlite3_bind_int(self.stmt_feed, 1, FEED_LIMIT);
            while libsqlite3_sys::sqlite3_step(self.stmt_feed) == libsqlite3_sys::SQLITE_ROW {
                if !first {
                    self.feed_json.push(b',');
                }
                first = false;
                let id = libsqlite3_sys::sqlite3_column_int64(self.stmt_feed, 0);
                let author_ptr = libsqlite3_sys::sqlite3_column_text(self.stmt_feed, 1);
                let author_len = libsqlite3_sys::sqlite3_column_bytes(self.stmt_feed, 1) as usize;
                let content_ptr = libsqlite3_sys::sqlite3_column_text(self.stmt_feed, 2);
                let content_len = libsqlite3_sys::sqlite3_column_bytes(self.stmt_feed, 2) as usize;
                let likes = libsqlite3_sys::sqlite3_column_int64(self.stmt_feed, 3);
                let created_at = libsqlite3_sys::sqlite3_column_int64(self.stmt_feed, 4);

                self.feed_json.extend_from_slice(b"{\"id\":");
                write_i64(&mut self.feed_json, id);
                self.feed_json.extend_from_slice(b",\"author\":");
                let author: &[u8] = if author_ptr.is_null() {
                    &[]
                } else {
                    slice::from_raw_parts(author_ptr, author_len)
                };
                write_escaped_json(&mut self.feed_json, author);
                self.feed_json.extend_from_slice(b",\"content\":");
                let content: &[u8] = if content_ptr.is_null() {
                    &[]
                } else {
                    slice::from_raw_parts(content_ptr, content_len)
                };
                write_escaped_json(&mut self.feed_json, content);
                self.feed_json.extend_from_slice(b",\"likes\":");
                write_i64(&mut self.feed_json, likes);
                self.feed_json.extend_from_slice(b",\"created_at\":");
                write_i64(&mut self.feed_json, created_at);
                self.feed_json.push(b'}');
            }
        }
        self.feed_json.push(b']');
        self.feed_valid = true;
    }
}

fn write_i64(buf: &mut Vec<u8>, mut n: i64) {
    if n == 0 {
        buf.push(b'0');
        return;
    }
    if n < 0 {
        buf.push(b'-');
        n = -n;
    }
    let mut temp = [0u8; 24];
    let mut i = 0;
    while n > 0 {
        temp[i] = (n % 10) as u8 + b'0';
        n /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        buf.push(temp[i]);
    }
}

fn write_escaped_json(buf: &mut Vec<u8>, s: &[u8]) {
    buf.push(b'"');
    for &b in s {
        match b {
            b'"' => buf.extend_from_slice(b"\\\""),
            b'\\' => buf.extend_from_slice(b"\\\\"),
            b'\n' => buf.extend_from_slice(b"\\n"),
            b'\r' => buf.extend_from_slice(b"\\r"),
            b'\t' => buf.extend_from_slice(b"\\t"),
            b => buf.push(b),
        }
    }
    buf.push(b'"');
}

/// Append a decimal integer at p in a byte buffer.
fn put_i64(buf: &mut [u8], p: &mut usize, mut n: i64) {
    if n == 0 {
        buf[*p] = b'0';
        *p += 1;
        return;
    }
    if n < 0 {
        buf[*p] = b'-';
        *p += 1;
        n = -n;
    }
    let mut temp = [0u8; 24];
    let mut i = 0;
    while n > 0 {
        temp[i] = (n % 10) as u8 + b'0';
        n /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        buf[*p] = temp[i];
        *p += 1;
    }
}

const HEAD_JSON: &[u8] = b"HTTP/1.1 200 OK\r\nServer: holy-rust-maxout\r\nContent-Type: application/json\r\nContent-Length: ";
const HEAD_TEXT: &[u8] = b"HTTP/1.1 200 OK\r\nServer: holy-rust-maxout\r\nContent-Type: text/plain\r\nContent-Length: ";
const NOT_FOUND: &[u8] =
    b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";

/// Render a full response into the slot's write buffer. No allocation.
fn respond(slot: &mut Slot, body: &[u8], is_json: bool) {
    let head = if is_json { HEAD_JSON } else { HEAD_TEXT };
    let mut p = 0usize;
    slot.write_buf[p..p + head.len()].copy_from_slice(head);
    p += head.len();
    put_i64(&mut slot.write_buf, &mut p, body.len() as i64);
    let conn: &[u8] = if slot.keep_alive {
        b"\r\nConnection: keep-alive\r\n\r\n"
    } else {
        b"\r\nConnection: close\r\n\r\n"
    };
    slot.write_buf[p..p + conn.len()].copy_from_slice(conn);
    p += conn.len();
    slot.write_buf[p..p + body.len()].copy_from_slice(body);
    p += body.len();
    slot.write_off = 0;
    slot.write_len = p;
    slot.state = SlotState::Writing;
}

fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || hay.len() < needle.len() {
        return None;
    }
    let first = needle[0];
    let last = hay.len() - needle.len();
    let mut i = 0;
    while i <= last {
        match hay[i..=last].iter().position(|&b| b == first) {
            Some(off) => {
                i += off;
                if &hay[i..i + needle.len()] == needle {
                    return Some(i);
                }
                i += 1;
            }
            None => return None,
        }
    }
    None
}

/// Parse one complete request out of `req`. Returns:
/// 1 = response ready, 2 = write queued, 0 = need more bytes.
fn parse_request(
    db: &mut Db,
    slot: &mut Slot,
    pending: &mut Vec<PendingWrite>,
    idx: usize,
    req: &[u8],
) -> i32 {
    let hdr_end = match find(req, b"\r\n\r\n") {
        Some(v) => v + 4,
        None => return 0,
    };

    let mut body_len = 0usize;
    if let Some(pos) = find(&req[..hdr_end], b"\r\nContent-Length: ") {
        let mut n = 0usize;
        for &b in &req[pos + 18..hdr_end] {
            if b.is_ascii_digit() {
                n = n * 10 + (b - b'0') as usize;
            } else {
                break;
            }
        }
        body_len = n;
    }
    if req.len() < hdr_end + body_len {
        return 0;
    }

    slot.keep_alive = find(&req[..hdr_end], b"Connection: close").is_none();
    let body = &req[hdr_end..hdr_end + body_len];

    if req.starts_with(b"GET /health") {
        respond(slot, b"{\"status\":\"ok\"}", true);
        return 1;
    }

    if req.starts_with(b"GET /feed") || req.starts_with(b"GET /posts") {
        db.build_feed();
        // Copy out before touching slot mutably: the borrow checker is right
        // that db.feed_json and slot are unrelated, but the copy also keeps
        // the send buffer fully owned by the connection.
        let mut feed = std::mem::take(&mut db.feed_json);
        respond(slot, &feed, true);
        feed.clear();
        db.feed_json = feed;
        return 1;
    }

    if req.starts_with(b"POST /posts") {
        if pending.len() >= BATCH_MAX {
            respond(slot, b"{\"error\":\"busy\"}", true);
            return 1;
        }
        let mut author = [0u8; 64];
        let mut content = [0u8; 256];
        let author_len = copy_field(body, b"\"author\":\"", &mut author);
        let content_len = copy_field(body, b"\"content\":\"", &mut content);
        pending.push(PendingWrite {
            slot_idx: idx,
            seq: slot.seq,
            kind: WriteKind::Insert {
                author,
                author_len,
                content,
                content_len,
                created_at: SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs() as i64,
            },
        });
        slot.state = SlotState::QueuedWrite;
        return 2;
    }

    if req.starts_with(b"POST /like") {
        if pending.len() >= BATCH_MAX {
            respond(slot, b"{\"error\":\"busy\"}", true);
            return 1;
        }
        let mut id = 1i64;
        if let Some(pos) = find(req, b"?id=") {
            let mut n = 0i64;
            for &b in &req[pos + 4..] {
                if b.is_ascii_digit() {
                    n = n * 10 + (b - b'0') as i64;
                } else {
                    break;
                }
            }
            if n > 0 {
                id = n;
            }
        }
        pending.push(PendingWrite {
            slot_idx: idx,
            seq: slot.seq,
            kind: WriteKind::Like { id },
        });
        slot.state = SlotState::QueuedWrite;
        return 2;
    }

    slot.write_buf[..NOT_FOUND.len()].copy_from_slice(NOT_FOUND);
    slot.write_off = 0;
    slot.write_len = NOT_FOUND.len();
    slot.state = SlotState::Writing;
    1
}

fn copy_field(body: &[u8], key: &[u8], dst: &mut [u8]) -> usize {
    if let Some(pos) = find(body, key) {
        let start = pos + key.len();
        if let Some(end) = body[start..].iter().position(|&b| b == b'"') {
            let n = end.min(dst.len() - 1);
            dst[..n].copy_from_slice(&body[start..start + n]);
            return n;
        }
    }
    0
}

fn release(slot: &mut Slot, free: &mut Vec<usize>, idx: usize) {
    if slot.state == SlotState::Free {
        return;
    }
    if slot.fd >= 0 {
        unsafe { libc::close(slot.fd) };
        slot.fd = -1;
    }
    slot.state = SlotState::Free;
    slot.seq = slot.seq.wrapping_add(1);
    free.push(idx);
}

/// Feed received bytes into a connection. Returns false if it must close.
fn feed_bytes(db: &mut Db, slot: &mut Slot, pending: &mut Vec<PendingWrite>, idx: usize, len: usize) -> bool {
    // Bytes were received into slot.recv_buf[0..len].
    if slot.state == SlotState::Writing || slot.state == SlotState::QueuedWrite {
        if slot.req_len + len > BUF_SIZE {
            return false;
        }
        slot.req_buf[slot.req_len..slot.req_len + len]
            .copy_from_slice(&slot.recv_buf[..len]);
        slot.req_len += len;
        return true;
    }

    if slot.req_len > 0 {
        if slot.req_len + len > BUF_SIZE {
            return false;
        }
        slot.req_buf[slot.req_len..slot.req_len + len]
            .copy_from_slice(&slot.recv_buf[..len]);
        slot.req_len += len;
        let n = slot.req_len;
        // Safe: parse_request only reads req_buf and writes other fields.
        let ptr = slot.req_buf.as_ptr();
        let req = unsafe { slice::from_raw_parts(ptr, n) };
        let r = parse_request(db, slot, pending, idx, req);
        if r == 0 {
            return true;
        }
        slot.req_len = 0;
        return true;
    }

    let ptr = slot.recv_buf.as_ptr();
    let req = unsafe { slice::from_raw_parts(ptr, len) };
    let r = parse_request(db, slot, pending, idx, req);
    if r == 0 {
        if len > BUF_SIZE {
            return false;
        }
        slot.req_buf[..len].copy_from_slice(&slot.recv_buf[..len]);
        slot.req_len = len;
    }
    true
}

/// Push one SQE, draining the submission queue first if it is full.
fn push_sqe(
    sq: &mut io_uring::SubmissionQueue<'_>,
    submitter: &mut io_uring::Submitter<'_>,
    sqe: &io_uring::squeue::Entry,
) {
    loop {
        match unsafe { sq.push(sqe) } {
            Ok(()) => return,
            Err(_) => {
                sq.sync();
                submitter.submit().expect("submit while SQ full");
            }
        }
    }
}

fn arm_send(
    sq: &mut io_uring::SubmissionQueue<'_>,
    submitter: &mut io_uring::Submitter<'_>,
    slot: &Slot,
    idx: usize,
) {
    let sqe = opcode::Send::new(
        types::Fd(slot.fd),
        unsafe { slot.write_buf.as_ptr().add(slot.write_off) },
        (slot.write_len - slot.write_off) as u32,
    )
    .build()
    .user_data(((idx as u64) << 2) | OP_SEND);
    push_sqe(sq, submitter, &sqe);
}

fn arm_recv(
    sq: &mut io_uring::SubmissionQueue<'_>,
    submitter: &mut io_uring::Submitter<'_>,
    slot: &Slot,
    idx: usize,
) {
    let sqe = opcode::Recv::new(types::Fd(slot.fd), slot.recv_buf.as_ptr() as *mut u8, BUF_SIZE as u32)
        .build()
        .user_data(((idx as u64) << 2) | OP_RECV);
    push_sqe(sq, submitter, &sqe);
}

fn arm_accept(
    sq: &mut io_uring::SubmissionQueue<'_>,
    submitter: &mut io_uring::Submitter<'_>,
    listen_fd: i32,
) {
    let sqe = opcode::AcceptMulti::new(types::Fd(listen_fd))
        .build()
        .user_data(OP_ACCEPT);
    push_sqe(sq, submitter, &sqe);
}

fn main() {
    let port: u16 = std::env::var("PORT").ok().and_then(|s| s.parse().ok()).unwrap_or(8080);
    let db_path = std::env::var("DB_PATH").unwrap_or_else(|_| "benchmark.db".to_string());
    println!("Rust maxout server on 0.0.0.0:{} (db {})", port, db_path);

    let listener = std::net::TcpListener::bind(("0.0.0.0", port)).expect("bind failed");
    let listen_fd = listener.as_raw_fd();
    unsafe {
        let opt: libc::c_int = 1;
        libc::setsockopt(listen_fd, libc::SOL_SOCKET, libc::SO_REUSEADDR,
                         &opt as *const _ as *const libc::c_void, std::mem::size_of_val(&opt) as u32);
        libc::setsockopt(listen_fd, libc::SOL_SOCKET, libc::SO_REUSEPORT,
                         &opt as *const _ as *const libc::c_void, std::mem::size_of_val(&opt) as u32);
        // std uses a backlog of 128; the benchmark wants more headroom.
        libc::listen(listen_fd, 4096);
        let flags = libc::fcntl(listen_fd, libc::F_GETFL, 0);
        libc::fcntl(listen_fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
    }

    let mut db = Db::open(&db_path);

    // Same io_uring feature set as the HolyPHP engine, with fallbacks.
    let mut ring = IoUring::builder()
        .setup_single_issuer()
        .setup_defer_taskrun()
        .build(RING_ENTRIES)
        .or_else(|_| {
            IoUring::builder()
                .setup_single_issuer()
                .setup_coop_taskrun()
                .build(RING_ENTRIES)
        })
        .unwrap_or_else(|_| IoUring::new(RING_ENTRIES).expect("io_uring init failed"));

    let mut slots: Vec<Slot> = (0..MAX_CONNECTIONS).map(|_| Slot::new()).collect();
    let mut free_slots: Vec<usize> = (0..MAX_CONNECTIONS).rev().collect();
    let mut pending: Vec<PendingWrite> = Vec::with_capacity(BATCH_MAX);

    let (mut submitter, mut sq, mut cq) = ring.split();
    arm_accept(&mut sq, &mut submitter, listen_fd);
    sq.sync();
    submitter.submit().expect("initial submit");

    let mut feed_body: Vec<u8> = Vec::with_capacity(64);

    loop {
        match submitter.submit_and_wait(1) {
            Ok(_) => {}
            Err(e) if e.raw_os_error() == Some(libc::EINTR) => continue,
            Err(e) => {
                eprintln!("submit_and_wait: {e}");
                break;
            }
        }
        cq.sync();

        while let Some(cqe) = cq.next() {
            let ud = cqe.user_data();
            let res = cqe.result();
            let flags = cqe.flags();

            if ud == OP_ACCEPT {
                if res >= 0 {
                    let client_fd = res;
                    unsafe {
                        let opt: libc::c_int = 1;
                        libc::setsockopt(client_fd, libc::IPPROTO_TCP, libc::TCP_NODELAY,
                                         &opt as *const _ as *const libc::c_void,
                                         std::mem::size_of_val(&opt) as u32);
                    }
                    if let Some(idx) = free_slots.pop() {
                        let s = &mut slots[idx];
                        s.fd = client_fd;
                        s.state = SlotState::Reading;
                        s.keep_alive = true;
                        s.req_len = 0;
                        s.write_len = 0;
                        s.write_off = 0;
                        arm_recv(&mut sq, &mut submitter, s, idx);
                    } else {
                        unsafe { libc::close(client_fd) };
                    }
                }
                if !cqueue::more(flags) {
                    arm_accept(&mut sq, &mut submitter, listen_fd);
                }
                continue;
            }

            let op = ud & 3;
            let idx = (ud >> 2) as usize;
            if idx >= MAX_CONNECTIONS {
                continue;
            }
            if slots[idx].state == SlotState::Free {
                continue;
            }

            match op {
                OP_RECV => {
                    if res <= 0 {
                        release(&mut slots[idx], &mut free_slots, idx);
                    } else {
                        let ok = feed_bytes(&mut db, &mut slots[idx], &mut pending, idx, res as usize);
                        if !ok {
                            release(&mut slots[idx], &mut free_slots, idx);
                        } else if slots[idx].state == SlotState::Writing {
                            arm_send(&mut sq, &mut submitter, &slots[idx], idx);
                        } else if slots[idx].state != SlotState::Free {
                            arm_recv(&mut sq, &mut submitter, &slots[idx], idx);
                        }
                    }
                }
                OP_SEND => {
                    if res <= 0 {
                        release(&mut slots[idx], &mut free_slots, idx);
                    } else {
                        let s = &mut slots[idx];
                        s.write_off += res as usize;
                        if s.write_off < s.write_len {
                            arm_send(&mut sq, &mut submitter, s, idx);
                        } else {
                            s.write_off = 0;
                            s.write_len = 0;
                            if !s.keep_alive {
                                release(&mut slots[idx], &mut free_slots, idx);
                            } else if s.req_len > 0 {
                                // Pipelined request waited behind the response.
                                let n = s.req_len;
                                s.req_len = 0;
                                s.state = SlotState::Reading;
                                let ptr = s.req_buf.as_ptr();
                                let req = unsafe { slice::from_raw_parts(ptr, n) };
                                let r = parse_request(&mut db, s, &mut pending, idx, req);
                                if r == 1 {
                                    arm_send(&mut sq, &mut submitter, &slots[idx], idx);
                                }
                            } else {
                                s.state = SlotState::Reading;
                                arm_recv(&mut sq, &mut submitter, s, idx);
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        // ---- group commit: one transaction for everything read this batch ----
        if !pending.is_empty() {
            unsafe {
                libsqlite3_sys::sqlite3_step(db.stmt_begin);
                libsqlite3_sys::sqlite3_reset(db.stmt_begin);

                for item in pending.drain(..) {
                    let s = &mut slots[item.slot_idx];
                    if s.state != SlotState::QueuedWrite || s.seq != item.seq {
                        continue;
                    }
                    match item.kind {
                        WriteKind::Insert { author, author_len, content, content_len, created_at } => {
                            libsqlite3_sys::sqlite3_reset(db.stmt_insert);
                            libsqlite3_sys::sqlite3_bind_text(db.stmt_insert, 1,
                                author.as_ptr() as *const _, author_len as i32,
                                libsqlite3_sys::SQLITE_STATIC());
                            libsqlite3_sys::sqlite3_bind_text(db.stmt_insert, 2,
                                content.as_ptr() as *const _, content_len as i32,
                                libsqlite3_sys::SQLITE_STATIC());
                            libsqlite3_sys::sqlite3_bind_int64(db.stmt_insert, 3, created_at);
                            libsqlite3_sys::sqlite3_step(db.stmt_insert);

                            let new_id = libsqlite3_sys::sqlite3_last_insert_rowid(db.db);
                            feed_body.clear();
                            feed_body.extend_from_slice(b"{\"id\":");
                            write_i64(&mut feed_body, new_id);
                            feed_body.extend_from_slice(b",\"status\":\"created\"}");
                            respond(s, &feed_body, true);
                        }
                        WriteKind::Like { id } => {
                            libsqlite3_sys::sqlite3_reset(db.stmt_like);
                            libsqlite3_sys::sqlite3_bind_int64(db.stmt_like, 1, id);
                            libsqlite3_sys::sqlite3_step(db.stmt_like);

                            feed_body.clear();
                            feed_body.extend_from_slice(b"{\"id\":");
                            write_i64(&mut feed_body, id);
                            feed_body.extend_from_slice(b",\"status\":\"liked\"}");
                            respond(s, &feed_body, true);
                        }
                    }
                    arm_send(&mut sq, &mut submitter, s, item.slot_idx);
                }

                libsqlite3_sys::sqlite3_step(db.stmt_commit);
                libsqlite3_sys::sqlite3_reset(db.stmt_commit);
                db.feed_valid = false;
            }
        }

        sq.sync();
    }
}
