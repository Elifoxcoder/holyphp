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

- the pointer-returning builtins (`sqlite_*`, `bt_*`, `iarr_data_ptr`, …) are
  called inside `unsafe { }` blocks;
- raw byte/word reads use the named accessors (`u8_at`, `i64_at`, `i64_set`)
  instead of `*p` / `*(p + i)`;
- a few locals are initialised before use (`$db = 0`, `$sp1 = 0`, …) because
  the checker needs a definite assignment.

No algorithm, SQL statement or response format was changed. The suite's golden
files still match byte for byte.

`fixture/feed.db` is 36 KiB: the 20 posts of the golden `/feed`, posts 1,
250000 and 500000, the 23 users they reference plus user 1, and all 98 likes
on those posts. See `fixture/README.md` for the generator, which cuts the same
rows out of the full seed.

## Why this is not in `tests/`

It binds a port and runs for several seconds per mode. The regular suite stays
headless; this is the integration benchmark, like `benchmarks/maxout/`.
