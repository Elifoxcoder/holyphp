# Benchmark results

Numbers are from the measurement scripts in this folder, on the branch and
date named in each section. They are relative comparisons on one machine, not
absolute performance claims.

## SQLite module (task 4.4)

`bash benchmarks/sqlite/run.sh --iters 50000 --repeat 15`, database copy on
tmpfs, opened with `unix-excl` like `benchmarks/challenge/start.sh`, compiler
built by `scripts/build.sh`. Machine: WSL2 Ubuntu 24.04, gcc 13.3.0, 32 vCPU.
Host frequency and other worktrees make single runs vary by up to 2x, so
changes are accepted or rejected with **paired A/B runs**: two bench binaries
(or two environment settings) alternate in one loop (`scratch/paired.sh`,
not committed) and the median of 21-25 rounds per side is compared. Absolute
ns/op below are one such median; 5-10% between tables is not a change.

Allocation counts come from `benchmarks/sqlite/malloc_count.c` (LD_PRELOAD over
glibc's allocator) with `BENCH_ITERS=0` subtracted; they are exact for the
measured loop and include the runtime's own `hstr` allocations.

### Baseline — `safe/sqlite-speed` @ 4437718, 2026-11-19

`-O2`, the challenge's original compile options.

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point (PK read) | 915 | 1.00 | 163 |
| range (20-row index scan) | 2 338 | 0.00 | 0 |
| feed (20-row JSON aggregate) | 35 049 | 28.00 | 21 377 |
| insert (indexed, 100/commit) | 3 067 | 0.05 | 224 |
| bt (20 cursor rows) | 550 | 0.00 | 0 |
| prepare (point statement) | 2 515 | 0.00 | 0 |

The `feed` row is where SQLite allocates: the JSON aggregate and its correlated
lookups build twenty-odd temporary buffers per request, all larger than a
lookaside slot. `point`'s single allocation is the runtime's `hstr` for the
returned text column; SQLite itself allocates nothing there. `bt` is the
challenge's read path (25 ns per cursor row).

The same run with SQLite's default VFS instead of `unix-excl` gives 5 766 ns
for `point` and 7 170 ns for `range`: every read transaction takes three
`fcntl` locks and one `fstat` (measured with `strace -c`). That is why the
benchmark mirrors the challenge's locking mode.

### Kept: compile SQLite with `-O3` and without unused features

Two changes, each measured paired against the previous step, all 21-25 rounds:

| Step | point | feed | insert | range | bt |
|---|---:|---:|---:|---:|---:|
| `-O2` → `-O3` | 931 → **893** | 39 018 → **37 490** | 3 293 → **3 207** | 2 409 → 2 393 | 549 → 545 |
| `-O3` → `-O3` + trims | 895 → **857** | 38 125 → **36 825** | 3 174 → 3 171 | 2 271 → **2 237** | 541 → 539 |

The trims are `SQLITE_OMIT_DEPRECATED`, `SQLITE_OMIT_DECLTYPE` and
`SQLITE_OMIT_COMPILEOPTION_DIAGS`; none of the removed APIs is exposed by the
HolyPHP binding. The size effect is small (archive 1 710 854 → 1 702 824
bytes), so the few percent are most likely code layout in the VDBE dispatch
loop - still reproducible in both orders.

Final absolute run (`-O3` + trims, 50 000 ops, 15 rounds):

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point | 872 | 1.00 | 163 |
| range | 2 253 | 0.00 | 0 |
| feed | 32 361 | 28.00 | 21 377 |
| insert | 3 082 | 0.05 | 215 |
| bt | 543 | 0.00 | 0 |
| prepare | 2 353 | 0.00 | 0 |

Malloc counts are unchanged: the remaining allocations are the JSON aggregate's
string buffers and the runtime's `hstr`, not SQLite bookkeeping.

### Rejected changes

| Change | Measurement | Why rejected |
|---|---|---|
| Lookaside 1200 × 400 instead of the default 1200 × 40 | feed: 28 → 28 malloc/op, 39.9 → 39.3 µs (noise) | +480 KiB per connection for nothing; the default already covers every small object. Turning lookaside *off* does hurt (28 → 119 malloc/op), so the default is worth keeping. |
| Page cache bulk of 512 pages instead of SQLite's 20 | paired, 21 rounds: point 936 → 953, range 2 362 → 2 409, feed 34 515 → 35 336; insert 3 195 → 3 181 | reads got slower, inserts did not get faster; 2 MiB per connection up front. Malloc bytes/op on insert drop (224 → 159), but not enough to pay for the memory. |
| `SQLITE_PREPARE_PERSISTENT` for `sqlite_prepare` | prepare: 2 552 → 2 136 ns (faster) but 0 → 23 malloc/op; point/feed unchanged | helps only the one-time prepare, adds heap churn; the hot path is unchanged. |
| `SQLITE_CONFIG_HEAP` (MEMSYS5) with a 64 MiB fixed heap | paired, 15 rounds: point 916 → 922, range 2 399 → 2 472, feed 38 285 → 38 448, insert neutral | no speed win, and a fixed heap smaller than page cache + working set fails with `SQLITE_NOMEM`; a default would break larger databases. |
| Statement journal off for single-statement writes | `strace` over the challenge's `INSERT ... RETURNING` and `INSERT ... SELECT ... ON CONFLICT` | SQLite creates no statement-journal file for them at all (also not with `INSERT OR REPLACE`), and the server runs with `temp_store=MEMORY`. Nothing to turn off. |

### Cost of not reusing statements and cursors (baseline)

| Variant | ns/op | malloc/op |
|---|---:|---:|
| point, statement prepared each iteration | 3 844 | 1.00 |
| bt, B-tree cursor reopened each iteration | 1 031 | 2.00 |

Reusing the prepared statement saves ~3.0 µs per point read; reusing the cursor
saves ~0.5 µs per 20-row cursor scan and two allocations. The challenge server
already keeps both open, so this table is the justification for not
re-preparing in the hot path.

## Earlier challenge results

The twelve-dollar challenge suite and its 42 checks live in
[`challenge/`](challenge/); see `STATUS.md` section 6 for the current pass
status. This file records only the SQLite micro benchmark.
