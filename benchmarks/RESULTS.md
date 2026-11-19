# Benchmark results

Numbers are from the measurement scripts in this folder, on the branch and
date named in each section. They are relative comparisons on one machine, not
absolute performance claims.

## SQLite module (task 4.4)

`bash benchmarks/sqlite/run.sh --iters 20000 --repeat 5`, database copy on
tmpfs, compiler built by `scripts/build.sh` (release, SQLite `-O2`).
Machine: WSL2 Ubuntu 24.04, gcc 13.3.0, 32 vCPU. Values are the smallest
ns/op of five runs; malloc counts are the marginal cost per operation
(`BENCH_ITERS=0` subtracted).

### Baseline — `safe/sqlite-speed` @ c458fc6, 2026-11-19

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point (PK read) | 5 758 | 1.00 | 162 |
| range (20-row index scan) | 7 413 | 0.00 | 0 |
| feed (20-row JSON aggregate) | 40 642 | 28.00 | 21 377 |
| insert (indexed, 100/commit) | 2 987 | 0.04 | 167 |
| bt (20 cursor rows) | 547 | 0.00 | 0 |
| prepare (point statement) | 2 505 | 0.00 | 0 |

The `feed` row is where SQLite allocates: JSON aggregation and the three
correlated lookups exceed the connection's 48 KiB default lookaside and fall
back to `malloc` 28 times per request. The `point`/`feed` counts also include
the runtime's `hstr` for each returned text column (162 bytes for `point`).

### Cost of not reusing statements and cursors (baseline)

| Variant | ns/op | malloc/op |
|---|---:|---:|
| point, statement prepared each iteration | 9 241 | 1.00 |
| bt, B-tree cursor reopened each iteration | 5 636 | 2.00 |

Reusing the prepared statement saves ~3.5 µs per point read; reusing the
cursor saves ~5.1 µs per 20-row cursor scan and two allocations. The challenge
server already keeps both open, so this table is the justification for not
re-preparing in the hot path (and for keeping cursor open/close out of it).

## Earlier challenge results

The twelve-dollar challenge suite and its 42 checks live in
[`challenge/`](challenge/); see `STATUS.md` section 6 for the current pass
status. This file records only the SQLite micro benchmark.
