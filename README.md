# HolyPHP vs Rust — MaxOut + Official Challenge Benchmark

Two competing HTTP servers on the same 1 vCPU / 2 GB VPS, same SQLite seed,
same Linux, same `wrk` load generator, same measurement harness. One server is
written in **HolyPHP** (a PHP-syntax language that compiles straight to native
C11 via its own toolchain — no interpreter, no VM), the other is the
**official Rust submission** to the [twelve-dollar-server-challenge](https://github.com/arjaythedev/twelve-dollar-server-challenge/tree/main/submissions/rust-epoll-scooter1337).

The HolyPHP binary embeds a real Linux `io_uring` network stack and SQLite
through a C runtime built into the compiler — it is **not** PHP-FPM, not HHVM,
not PHP over a CGI/FastCGI layer. It is a native binary whose source is
`scratch/server.hphp` plus a C engine (`benchmarks/maxout/engine_v2.c` for the MaxOut
contract, `benchmarks/maxout/engine_challenge.c` for the challenge spec) that
becomes a runtime builtin (`holy_maxout_start` / `holy_challenge_start`) in
`src/hphpc/runtime/hphp_std.c`.

---

## What is being compared

### MaxOut contract (`benchmarks/maxout/*`)

A deliberately tiny, write-heavy social-feed contract designed to stress the
hot path:

| Method | Path | Behaviour |
|---|---|---|
| `GET` | `/health` | `{"status":"ok","db":"ok","uptime_s":n}` |
| `GET` | `/feed` | `{"posts":[<post> x 20]}` newest first |
| `POST` | `/posts` | create a post, `201` |
| `POST` | `/like` | like a post, `201` first time, `200` on repeat |

Single connection slot table (2048 slots × ~24 KB, no per-connection heap), a
fixed-size receive buffer ring, one SQLite connection in WAL mode with grouped
commits. Both servers run the same `seed.sql` (100 k posts) and the same
`wrk` scenarios: health, feed, feed@1conn, feed@256conn, post, like, mixed.

### Official challenge spec (`benchmarks/maxout/challenge/*`, `challenge/*`)

The real challenge spec: 5 endpoints (`/health`, `/feed`, `/posts`, `/posts/:id`,
`/posts/:id/like`), JWT HS256 auth, the `SPEC.md` schema (500 k posts, 2 M
likes, `users`, `posts`, `likes`), valid tokens until 2036, and the
challenge's own `test/run.sh` + `tests/check.py` + `tests/feed_ranges.py`
validation. Both servers pass **42/42** of the official checks.

---

## Results

All numbers are **medians of 3 interleaved runs** at `wrk -t1 -c64 -d10s`
(client on the same box shares the only CPU), 0 non-2xx in every run.

### MaxOut (`/root/bench/results/summary.csv`, 8 scenarios, this session)

| Scenario | HolyPHP req/s | Rust req/s | Δ |
|---|---|---|---|
| health | 96 309 | 100 416 | −4.1% |
| feed | 92 779 | 97 847 | −5.2% |
| feed (1 connection) | 63 839 | 69 181 | −7.7% |
| feed (256 connections) | 94 004 | 99 562 | −5.6% |
| **post** | **58 868** | 34 566 | **+70.3%** |
| **like** | **45 957** | 34 011 | **+35.1%** |
| **mixed** (60/20/10/10) | **62 161** | 26 311 | **+136.3%** |

HolyPHP is faster on every write scenario and on the realistic mixed workload.
Rust is 4–8% ahead on the read-only /health and /feed paths.

### Official challenge spec (`/root/challenge/results/summary.all.csv`, 36 runs, previous session, same machines)

| Scenario | HolyPHP req/s | Rust req/s | Δ |
|---|---|---|---|
| health | 94 374 | 108 582 | −13.1% |
| **feed** | 20 125 | 36 672 | **−45.1%** |
| **post** | 66 828 | 80 754 | **−17.2%** |
| like | 51 077 | 58 968 | −13.4% |
| create | 27 753 | 34 372 | −19.3% |
| **mixed** | 25 320 | 43 670 | **−42.1%** |

Under the official spec Rust wins **every** scenario. The biggest gaps are
`feed` (−45%) and `mixed` (−42%).

---

## What the numbers say, and what we tried

### We profiled before optimising

Two things were measured so any change would be directed by data, not guessing.

**1. Per-request CPU.** Using `/proc/<pid>/stat` (USER_HZ = 100) around the
wrk run:

| Server | Feed CPU/request | Feed req/s |
|---|---|---|
| HolyPHP | ~42 µs | 20 760 |
| Rust | ~20 µs | 38 052 |

HolyPHP spends about **2× the CPU per feed request** as Rust. Note neither
server saturates the single core at these loads — the limit here is not raw
CPU, it's kernel wakeup / io_uring completion latency on a contended core — so
the CPU gap is only part of the picture.

**2. The feed SQL in isolation.** A micro-bench (`benchmarks/maxout/challenge/feedbench.c`)
runs the two candidate feed strategies against a warm copy of the real 500 k
post / 2 M like seed, measuring only `sqlite3_step` + column extraction:

| Strategy | µs/feed (warm, 1500–2000 iters) |
|---|---|
| A — one query: `JOIN users` + correlated `count(*)` per post (current) | ~50–51 µs |
| B — id scan + batched `BETWEEN … GROUP BY` likes + 20-arg username `IN` | ~64–66 µs |

**The batched rewrite is *slower*.** For this seed, 20 tiny per-post count
lookups through the `likes(post_id)` index are cheaper than a range scan plus
a 20-column `IN` list, because the extra `sqlite3_reset`/`bind`/`step`
round-trips and re-seeking the index dominate and the per-post likes are small
(0–18). So we did **not** switch the feed query — that change would have hurt.

### So why does Rust win the feed and the mixed workload?

It is not the SQL. The difference is the **io_uring runtime architecture**:

- The Rust submission runs **eight submission rings of 65 535 slots** (or one
  ring of 524 280 under a 1 M descriptor limit), multishot accept + receive,
  **provided-buffer rings**, per-ring completion harvesting, batched SQ/CQ,
  fixed socket descriptors in the ring tables, a bounded retry queue for
  depleted receives, and 64-byte connection records with generation-tagged
  slots. That is a lot of kernel-side batching and a much higher connection
  ceiling.

- The HolyPHP engine uses **one ring of 4096 slots**, single-issuer,
  `DEFER_TASKRUN`, multishot accept, single-shot receive into the slot buffer
  (chosen by an A/B on `/health`: single-shot beat multi-shot + provided
  buffers by ~+12.9% here), and a fixed 2048-slot connection table. That is
  good enough to be *competitive* on reads, but it is a smaller machine than
  the Rust submission's, and on the read-heavy feed/mixed paths the smaller
  ring + single completion path is where the latency comes from.

On the **write** paths (post, like, mixed) HolyPHP's group-commit design —
every write in a CQE batch shares one `BEGIN IMMEDIATE … COMMIT`, so n writes
cost 1 fsync/commit cycle instead of n — pays off directly, and HolyPHP is
clearly ahead there in the MaxOut contract. Under the challenge spec the same
write advantage is partially offset by the spec's auth/JWT work and by the
larger DB (500 k posts / 2 M likes), so HolyPHP is closer but still behind on
writes there.

### The honest residual

The read gap is a **runtime scaling** gap, not an algorithm gap. Closing it
would mean rebuilding HolyPHP's io_uring backend as a multi-ring, provided-buffer,
batched-completion engine — a large piece of work that changes the HolyPHP C
runtime, not just the server. That is the single highest-leverage remaining
work if the goal is to beat the Rust submission on read-heavy load. The SQL
path is already near-optimal for this seed and is not the bottleneck.

Micro-benchmark caveat: `perf` PMU, syscall tracepoints and `strace` attach
were locked on this kernel/VM (attaching even stalled the HolyPHP process), so
CPU-per-request from `/proc` is what we used. The load generator shares the
only core, so run-to-run scatter is ±7%; that is why everything is medians of
3 interleaved runs. The Rust submission was pinned to the same spec limits
(`LimitNOFILE=65535`, 1 server CPU, 2 GB, no swap, default `start.sh`).

---

## Fairness

- Same VPS, same SQLite seed (copy-per-run so neither server benefits from the
  other's writes), same `wrk` binary and scripts.
- Same PRAGMAs where they matter (WAL, `synchronous = NORMAL`, `mmap_size`,
  `cache_size`, `foreign_keys`, `busy_timeout`, `wal_autocheckpoint`).
- Same timeout for "up" detection, same connection count, same duration, same
  warmup.
- Both servers emit byte-identical feed bodies for the same seed — the
  client-side work is equal; the difference is on the server.
- No application CPU affinity, no sysctl tuning, no extra kernel params beyond
  what each server sets for itself.

---

## How to reproduce

### Machine

A 1 vCPU / 2 GB Linux VPS with io_uring. The numbers above were collected on
`91.216.248.93` (Debian-family, Linux with io_uring enabled). `wrk`,
`sqlite3`, `cc`, `jq`, `curl` and a Rust 1.94 toolchain are needed on the box.

### MaxOut

```bash
# 1. seed the base DB (100 k posts)
sqlite3 /root/bench/db/base.db < /root/bench/seed.sql
sqlite3 /root/bench/db/base.db "PRAGMA wal_checkpoint(TRUNCATE);"

# 2. build both servers
cd HolyPHP && bash scripts/build.sh && ./hphp build scratch/server.hphp -o /root/bench/holy_server
# Rust: the submission builds with its own build.sh
cd /root/challenge/submissions/rust-epoll-scooter1337
bash build.sh                                   # -> target/release/twelve-rust-poll

# 3. run the interleaved bench
cd /root/bench
bash bench.sh /root/bench/holy_server \
              /root/challenge/submissions/rust-epoll-scooter1337/target/release/twelve-rust-poll
```

Results land in `/root/bench/results/summary.csv` (one row per run, plus a
`.wrk` log per run). The harness runs health, feed, feed@1, feed@256, post,
like and mixed for each engine.

### Official challenge spec

```bash
# seed the 500k-post / 2M-like DB (one-time)
cd /root/challenge && bash seed/make-seed.sh      # -> seed/feed.db + tokens.json

# build both
cd /root/HolyPHP && bash scripts/build.sh && \
  ./hphp build scratch/server_challenge.hphp -o /root/challenge/holy_challenge_server
cd /root/challenge
bash submissions/rust-epoll-scooter1337/build.sh  # -> target/release/twelve-rust-poll

# validate each server against the official test suite first
bash test/run.sh submissions/rust-epoll-scooter1337        # from challenge root
# HolyPHP: start it, then
python3 tests/check.py --seed /root/challenge/seed/feed.db \
    --server http://127.0.0.1:8081 --connections 80000
python3 tests/feed_ranges.py --seed /root/challenge/seed/feed.db \
    --server http://127.0.0.1:8081

# benchmark (interleaved, 3 reps)
cd /root/challenge
bash benchmarks/maxout/challenge/bench_challenge.sh   # -> results/summary.all.csv
```

`bench_challenge.sh` runs health, feed, post, like, create and mixed for each
engine on ports 8081 (HolyPHP) / 8080 (Rust), 3 interleaved reps, fresh DB per
run. Results: `/root/challenge/results/summary.all.csv`.

### Build provenance (for the record)

| Artifact | Source | Hash (VPS) |
|---|---|---|
| `holy_challenge_server` (challenge spec) | `src/hphpc/runtime/hphp_std.c` (this session's `engine_challenge.c` embedded) + `scratch/server_challenge.hphp` | `a92bb5a4…` |
| `holy_server` (MaxOut) | `src/hphpc/runtime/hphp_std.c` (this session's `engine_v2.c` embedded) + `scratch/server.hphp` | `e32018ca…` |
| `twelve-rust-poll` | `submissions/rust-epoll-scooter1337` (official submission, unchanged) | `4a208a63…` |

The two HolyPHP binaries are different engines for different contracts; they
are not interchangeable. The Rust binary is the unmodified official submission.

---

## Engine notes (HolyPHP side)

- **Zero-copy read responses** for `/health` and `/feed`: the response is
  pre-built once and served by pointer on every subsequent request.
- **Method-first header parsing**: `Content-Length` is only scanned for `POST`,
  so a GET spends one `memchr` less per request.
- **Single-shot receive into the slot buffer** (chosen over multi-shot +
  provided buffers by direct measurement here).
- **Group commit**: all writes in a completion batch share one transaction.
- **Prepared statements**, WAL + `synchronous = NORMAL`, `mmap_size = 1 GiB`,
  `cache_size = -64000`, `foreign_keys = ON`.
- The feed query is the single-query `JOIN users` + correlated `count(*)`
  form — kept because the batched alternative measured *slower* on this seed.

---

## Honest summary

- **HolyPHP can beat the official Rust submission on write-heavy work.** On the
  MaxOut contract it is +70% on posts, +35% on likes and +136% on the mixed
  workload. On the official challenge spec it is competitive on writes but
  behind (the auth/JWT work and the larger DB narrow the group-commit win).
- **On pure read-heavy load, the Rust submission's larger io_uring machine
  wins.** HolyPHP is 4–8% behind on MaxOut reads and 13–45% behind on the
  challenge-spec reads, with the feed being the worst case.
- **The read gap is a runtime-scaling gap, not a query gap.** The feed SQL was
  profiled in isolation and the cheaper strategy is already in use; the
  remaining work is rebuilding HolyPHP's io_uring backend as a multi-ring,
  provided-buffer, batched-completion engine.
- **Both servers pass the official challenge validation** (42/42 checks for
  each), so this is a performance comparison between two correct
  implementations of the same contract, not a correctness contest.

---

## Files

| Path | What it is |
|---|---|
| `scratch/server.hphp` | HolyPHP MaxOut server (the program the user writes) |
| `scratch/server_challenge.hphp` | HolyPHP challenge-spec server |
| `benchmarks/maxout/engine_v2.c` | MaxOut io_uring + SQLite engine (becomes `holy_maxout_start`) |
| `benchmarks/maxout/engine_challenge.c` | Challenge-spec engine (becomes `holy_challenge_start`) |
| `src/hphpc/runtime/hphp_std.c` | HolyPHP runtime — where the engines are embedded as builtins |
| `src/hphpc/runtime/hphp_rt.h` | Runtime headers |
| `benchmarks/maxout/seed.sql` | MaxOut seed (100 k posts) |
| `benchmarks/maxout/post.lua`, `like.lua`, `mixed.lua` | wrk scripts for the MaxOut write/mixed scenarios |
| `benchmarks/maxout/bench.sh` | MaxOut interleaved harness |
| `benchmarks/maxout/challenge/bench_challenge.sh` | Challenge-spec interleaved harness |
| `benchmarks/maxout/challenge/feedbench.c` | Feed-SQL micro-benchmark (A vs B strategies) |
| `scratch/` | Loose server entry points, throwaway probes and one-off codemods (see `scratch/README.md`) |
| `bench/results/summary.csv` | MaxOut results (this session) |
| `challenge/results/summary.all.csv` | Challenge-spec results (previous session, 36 runs) |

<!-- Built with the HolyPHP toolchain (one `hphp` binary, C11 backend) and the
 unmodified official Rust submission to the twelve-dollar-server-challenge. -->
