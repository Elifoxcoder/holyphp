# MaxOut: HolyPHP vs Rust — benchmark results

A minimal Twitter-style API server in two languages, run against each other on
the same box, same database, same load generator.

* **Hardware**: 1 vCPU, 2 GB RAM, Ubuntu Linux 7.0 (VPS `91.216.248.93`)
* **Load generator**: `wrk -t1 -c64 -d10s --latency`, client on the same box
  (it shares the one core with the server — see *Fairness*)
* **Method**: 3 full interleaved repetitions of the suite, **medians** reported
* **Databases**: each run gets a fresh copy of a 100 000-row `posts` table
* **Date**: 2026-10-28

## Headline numbers (median of 3 runs)

| Scenario | Endpoint / script | HolyPHP | Rust | Holy vs Rust |
|---|---|---:|---:|---:|
| health | `GET /health` | 96 308 | 100 416 | **−4.1 %** |
| feed | `GET /feed` | 91 395 | 98 309 | **−7.0 %** |
| feed, 1 conn | `GET /feed` (serial) | 64 075 | 68 288 | **−6.2 %** |
| feed, 256 conn | `GET /feed` | 94 004 | 102 226 | **−8.0 %** |
| post | `POST /posts` (group commit) | 58 868 | 34 566 | **+70.3 %** |
| like | `POST /like?id=` | 45 957 | 34 067 | **+34.9 %** |
| **mixed** | 60 % feed / 20 % health / 10 % post / 10 % like | **62 161** | **22 500** | **+176.3 %** |

Read-only traffic (pure `recv → render → send`) is a near tie, with Rust ahead
by 4–8 %. Everything that writes is HolyPHP's by a wide margin, and on the
realistic mixed workload — the one that models the actual application —
**HolyPHP is 2.76× faster**.

Per-repetition numbers (all three runs, `summary.1.csv` … `summary.3.csv`):

| Scenario | rep 1 H / R | rep 2 H / R | rep 3 H / R | median H / R |
|---|---|---|---|---|
| health | 97 465 / 100 905 | 85 025 / 99 834 | 96 309 / 100 416 | 96 308 / 100 416 |
| feed | 91 395 / 98 309 | 91 077 / 100 132 | 92 779 / 97 847 | 91 395 / 98 309 |
| feed_c1 | 64 840 / 68 170 | 64 075 / 68 288 | 63 839 / 69 181 | 64 075 / 68 288 |
| feed_c256 | 93 357 / 102 667 | 94 221 / 102 226 | 94 004 / 99 562 | 94 004 / 102 226 |
| post | 60 338 / 34 509 | 57 202 / 34 759 | 58 868 / 34 566 | 58 868 / 34 566 |
| like | 46 226 / 35 062 | 45 663 / 34 067 | 45 957 / 34 011 | 45 957 / 34 067 |
| mixed | 62 967 / 22 500 | 61 905 / 21 332 | 62 161 / 26 311 | 62 161 / 22 500 |

## Where the remaining read-path gap went

Server CPU time per request (10 s run, `utime+stime` from `/proc/<pid>/stat`,
which includes kernel time charged to the process):

| | /feed | /health |
|---|---|---|
| HolyPHP | 3.88 µs/req (0.38 core) | 3.90 µs/req (0.37 core) |
| Rust | 3.50 µs/req (0.35 core) | 3.68 µs/req (0.39 core) |

Neither server is CPU-saturated — `wrk` takes the rest of the core — so the
0.2–0.4 µs of extra server work per request translates directly into fewer
requests per second. The wire work is provably identical (see *Fairness*), so
the difference is inside the loop, not in the response.

## Tuning experiments

Every switch below was measured interleaved, not guessed.

**1. Receive strategy (5 reps × 5 s, alternating order)** — `ab2.sh`

| | health median | feed median |
|---|---:|---:|
| multishot recv + provided buffers (`HPHP_MR=1`) | 92 754 | 97 511 |
| **single-shot recv into the slot** (`HPHP_MR=0`, new default) | **104 748** | **98 935** |

Single-shot recv won health by +12.9 % and feed by +1.5 %. The buffer ring
costs a consume atomic in the kernel *and* a return (two atomics plus a
release store) on every single request; on this kernel the plain recv that
copies straight into the connection's own buffer is cheaper. Multishot recv
stays available behind `HPHP_MR=1`.

**2. Task-run mode (3 reps × 6 s, alternating order)** — `ab3.sh`

| | health median | feed median |
|---|---:|---:|
| `DEFER_TASKRUN` (default) | 101 832 | 97 450 |
| `COOP_TASKRUN` (`HPHP_DEFER=0`) | 104 415 | 98 266 |

Inside the noise band (±7 % between reps), so `DEFER_TASKRUN` stays the
default — it is also what the Rust reference uses, so the comparison stays
apples-to-apples.

**3. Engine v2 → v3** (this change), measured as the whole-suite ratio:

| | v2 | v3 |
|---|---:|---:|
| health (holy ÷ rust) | 0.924 | 0.959 |
| feed (holy ÷ rust) | 0.887 | 0.930 |

## Fairness

* **Byte-identical bodies.** Against the same seed database both servers
  return `md5 5149386730a6b62c505b57472af2e2fe`, 2300 bytes for `/feed` and
  15 bytes for `/health`, with the same four response headers (the `Server:`
  value differs by 2 bytes). `wrk` therefore does identical client-side work.
* **Identical SQLite setup**: WAL, `synchronous = NORMAL`, `cache_size =
  -64000`, `mmap_size = 256 MiB`, `wal_autocheckpoint = 4096`, prepared
  statements, and the same `BEGIN IMMEDIATE … COMMIT` group-commit shape.
* **Identical io_uring shape**: 4096 ring entries, `SINGLE_ISSUER` +
  `DEFER_TASKRUN`, multi-shot accept, `TCP_NODELAY`, backlog 4096,
  `SO_REUSEADDR`/`SO_REUSEPORT`.
* **Fresh database per scenario**, copied from one seed, so no run inherits
  another's writes.
* **Interleaved**: HolyPHP runs, then Rust, per scenario, three times over.

## Reproducing

```bash
# on the VPS
cd /root/bench
./bench.sh /root/bench/holy_server \
           /root/benchmark/rust_server/target/release/rust_server
cat results/summary.csv

# three repetitions with medians (what produced the tables above)
bash run3.sh
```

Building the two servers:

```bash
# HolyPHP: engine lives in the runtime, so the compiler is rebuilt first
cd HolyPHP && bash scripts/build.sh
./hphp build scratch/server.hphp -o /root/bench/holy_server

# Rust
cd rust_server && cargo build --release
```

## Limitations

* The load generator shares the single core with the server, so absolute
  numbers are lower than a two-box setup would give, and run-to-run spread is
  ±7 %. Everything here is a median of three interleaved runs for that reason.
* Rust used default `--release` settings (`opt-level = 3`, no LTO, no PGO,
  no `target-cpu=native`). Adding LTO/PGO would move the Rust baseline up.
* `perf` hardware counters, syscall tracepoints and `strace` attachment were
  all unavailable/broken on this kernel/VM, so the read-path diagnosis leans
  on CPU-time-per-request instead of instruction counts.
