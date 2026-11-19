# SQLite micro benchmark

Measures the cost of the runtime's SQLite path on its own, without HTTP,
io_uring or the event loop: the same shapes the challenge server runs, over a
copy of [`benchmarks/challenge/fixture/feed.db`](../challenge/fixture/feed.db).

```bash
bash benchmarks/sqlite/run.sh                     # 20 000 ops, 5 timed runs
bash benchmarks/sqlite/run.sh --iters 50000 --repeat 5
BENCH_PREPARE_EACH=1 bash benchmarks/sqlite/run.sh --only point
BENCH_REOPEN_CURSOR=1 bash benchmarks/sqlite/run.sh --only bt
```

Each workload is timed around its own loop (`hrtime(true)`, ns resolution):

| name | what runs per operation |
|---|---|
| `point` | `SELECT body, user_id FROM posts WHERE id = ?` (primary key) |
| `range` | 20-row index range scan, `created_at DESC` order |
| `feed` | the challenge's feed query: 20 rows, `json_group_array`, like counts |
| `insert` | one small indexed row, committed in groups of 100 |
| `bt` | `bt_last` + 19 × `bt_prev` on `posts_created_at_id_idx` |
| `prepare` | `sqlite_prepare` + `sqlite_finalize` of the point query |

`run.sh` runs every workload in a fresh process on a fresh database copy (on
tmpfs when available, so disk I/O is not the variable), opens it with the
challenge's `unix-excl` VFS (no per-transaction file locking;
`BENCH_VFS=default` uses SQLite's default, `BENCH_VFS=` names another VFS),
reports the median of `--repeat` timed runs (`BENCH_MIN=1` also prints the
minimum), and then counts allocations with [`malloc_count.c`](malloc_count.c),
an `LD_PRELOAD` shim over glibc's `__libc_malloc` family. The counting run with
`BENCH_ITERS=0` includes the same setup and warm-up; subtracting it leaves the
marginal malloc calls and bytes per measured operation.

`BENCH_ONLY=<name>` runs one workload, which is how the malloc count is
attributed per operation. The reported malloc numbers include the language
runtime's own allocations (for `point` and `feed`, the `hstr` returned by
`sqlite_column_text`), not only SQLite's.

Comparing two builds needs paired runs, because host frequency and other work
on the machine move a single series by up to 2x: [`paired.sh`](paired.sh)
alternates two bench binaries within one loop and reports the median of each
side. It is how the accepted and rejected changes in
[`../RESULTS.md`](../RESULTS.md) were measured.

The project compiles SQLite with the challenge's options; see
[`third_party/sqlite/README.md`](../../third_party/sqlite/README.md) and the
`SQLITE_DEFS`/`HPHP_SQLITE_CFLAGS` lines in `scripts/build.sh`.

Results are kept in [`../RESULTS.md`](../RESULTS.md).

Needs Linux/WSL: the malloc shim uses glibc's `__libc_*` entry points. This is
a measurement tool, not part of `tests/run.sh`.
