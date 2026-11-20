# Challenge benchmark

The twelve-dollar challenge is a small authenticated feed API (five endpoints,
SQLite with WAL, keep-alive HTTP/1.1) from an external benchmark bundle. This
folder keeps the bundle's own server source and its 42-check acceptance suite
in the repository, so the SQLite and io_uring runtime modules are proven
against something larger than unit tests — and from a clean checkout, without
the 250 MB seed database.

```bash
bash benchmarks/challenge/run.sh              # direct (io_uring) + classic
bash benchmarks/challenge/run.sh direct       # HPHP_DIRECT=1 only
bash benchmarks/challenge/run.sh classic      # HPHP_DIRECT=0 only
```

Requirements: `bash`, `curl`, `jq`, `openssl`. The compiler is built by
`scripts/build.sh` when `./hphp` is missing. Direct mode needs Linux (io_uring);
elsewhere the runtime falls back to the classic real-fd path, and
`HPHP_DIRECT=0` forces that path.

The run uses a fresh copy of `fixture/feed.db` for each mode, on
`127.0.0.1:3000` (override with `PORT`), with the challenge's `JWT_SECRET`.
It prints the 42 `ok`/`FAIL` lines and exits non-zero on the first failing
mode.

## What is committed, and where it came from

| File | Origin |
|---|---|
| `server.hphp` | the bundle's submission server, adapted to this compiler |
| `start.sh` | the bundle's start script, unchanged (it tunes the runtime) |
| `test/test.sh`, `test/golden/*.json` | the bundle's acceptance suite, unchanged |
| `fixture/feed.db` | the rows of the bundle's seed database that the suite reads |

`server.hphp` differs from the bundle only in what this compiler requires:

- the pointer-returning builtins (`sqlite_*`, `bt_*`, `iarr_*`, `u8_at`,
  `i64_at`, `sb_take`, …) are called inside `unsafe { }` blocks, each call
  wrapped on its own;
- raw byte/word reads use the named accessors (`u8_at`, `i64_at`, `i64_set`)
  instead of `*p` / `*(p + i)`;
- a few locals are initialised before use (`$db = 0`, `$sp1 = 0`, …) because
  the checker needs a definite assignment;
- strings, arrays and objects are reference-counted now, so the server does
  not free them by hand (`str_free`, `arr_free`, `object_free`, `sb_discard`
  and the identity `str_persist` calls are gone); the pending-write queue owns
  its strings;
- the per-connection maps are probed with `array_key_exists()` instead of
  `isset($this->out[$fd])`, and the `/posts` route compares its one-character
  suffix with `str_at_eq()` (see `.agent/REPORT-opencode-6.md` for the
  compiler reproducers behind both workarounds).

No algorithm, SQL statement or response format was changed. The suite's golden
files still match byte for byte.

`fixture/feed.db` is 36 KiB: the 20 posts of the golden `/feed`, posts 1,
250000 and 500000, the 23 users they reference plus user 1, and all 98 likes
on those posts. See `fixture/README.md` for the generator, which cuts the same
rows out of the full seed.

## Load measurement

`scripts/bench-challenge.sh` starts the server through `start.sh` on a fresh
fixture copy, warms it up and records requests/s, p50/p95/p99 and RSS per run
with `oha` (Linux, io_uring):

```bash
bash scripts/bench-challenge.sh --root . --tag new --path /feed
bash scripts/bench-challenge.sh --root ../hp-main-bench --tag old --path /feed
```

`--root` points at the checkout whose `hphp` builds the server, so the same
command measures an older compiler. `.agent/REPORT-opencode-6.md` has the
reference table (30 s, 256 connections, three fresh-server runs per endpoint).

## Why this is not in `tests/`

It binds a port and runs for several seconds per mode. The regular suite stays
headless; this is the integration benchmark, like `benchmarks/maxout/`.
