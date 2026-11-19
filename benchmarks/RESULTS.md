# Benchmark results

Numbers are from the measurement scripts in this folder, on the branch and
date named in each section. They are relative comparisons on one machine, not
absolute performance claims.

## SQLite module (task 4.4)

`bash benchmarks/sqlite/run.sh --iters 30000 --repeat 15`, database copy on
tmpfs, opened with `unix-excl` like `benchmarks/challenge/start.sh`, compiler
built by `scripts/build.sh` (release, SQLite `-O2`), pinned with
`taskset -c 4`. Machine: WSL2 Ubuntu 24.04, gcc 13.3.0, 32 vCPU. Values are
the smallest ns/op of fifteen runs; malloc counts are the marginal cost per
operation (`BENCH_ITERS=0` subtracted).

### Baseline — `safe/sqlite-speed` @ 4437718, 2026-11-19

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point (PK read) | 889 | 1.00 | 163 |
| range (20-row index scan) | 2 321 | 0.00 | 0 |
| feed (20-row JSON aggregate) | 33 181 | 28.00 | 21 377 |
| insert (indexed, 100/commit) | 2 952 | 0.05 | 224 |
| bt (20 cursor rows) | 507 | 0.00 | 0 |
| prepare (point statement) | 2 366 | 0.00 | 0 |

The `feed` row is where SQLite allocates: JSON aggregation and the three
correlated lookups produce twenty-odd temporary buffers per request, none of
which fit the connection's lookaside area. The `point` count is the runtime's
`hstr` for the returned text column (163 bytes) - SQLite itself allocates
nothing there. `bt` is the challenge's read path: 25 ns per cursor row.

Opened with SQLite's default VFS instead of `unix-excl`, the same run gives
5 766 ns for `point` and 7 170 ns for `range`: every read transaction takes
three `fcntl` locks and one `fstat` (measured with `strace -c`). That is why
the benchmark mirrors the challenge's locking mode.

### Cost of not reusing statements and cursors (baseline)

| Variant | ns/op | malloc/op |
|---|---:|---:|
| point, statement prepared each iteration | 3 844 | 1.00 |
| bt, B-tree cursor reopened each iteration | 1 031 | 2.00 |

Reusing the prepared statement saves ~3.0 µs per point read; reusing the
cursor saves ~0.5 µs per 20-row cursor scan and two allocations. The challenge
server already keeps both open, so this table is the justification for not
re-preparing in the hot path.

## Earlier challenge results

The twelve-dollar challenge suite and its 42 checks live in
[`challenge/`](challenge/); see `STATUS.md` section 6 for the current pass
status. This file records only the SQLite micro benchmark.
