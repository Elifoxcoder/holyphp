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
(or two environment settings) alternate in one loop
([`sqlite/paired.sh`](sqlite/paired.sh)) and the median of 21-25 rounds per
side is compared. Absolute ns/op below are one such median; 5-10% between
tables is not a change.

Allocation counts come from `benchmarks/sqlite/malloc_count.c` (LD_PRELOAD over
glibc's allocator) with `BENCH_ITERS=0` subtracted; they are exact for the
measured loop and include the runtime's own `hstr` allocations.

### Baseline — `safe/sqlite-speed` @ 4437718, 2026-11-19

`-O2`, the challenge's original compile options, and SQLite's default 20-page
bulk page cache. Measured in the same paired loop as the final build below
(median of 15 rounds per side, page-cache setting equalised), so the two
columns are comparable.

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point (PK read) | 983 | 1.00 | 163 |
| range (20-row index scan) | 2 452 | 0.00 | 0 |
| feed (20-row JSON aggregate) | 35 260 | 28.00 | 21 377 |
| insert (indexed, 100/commit) | 3 301 | 0.05 | 224 |
| bt (20 cursor rows) | 546 | 0.00 | 0 |
| prepare (point statement) | 2 480 | 0.00 | 0 |

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

Two changes, each measured paired against the previous step, all 15 rounds,
with the page-cache setting equalised (`HPHP_SQLITE_PAGECACHE=20`, SQLite's
default, for both binaries):

| Step | point | range | feed | insert | bt | prepare |
|---|---:|---:|---:|---:|---:|---:|
| `-O2` → `-O3` | 945 → **935** | 2 518 → **2 462** | 36 553 → **35 498** | 3 426 → **3 293** | 547 → 550 | 2 523 → 2 546 |
| `-O3` → `-O3` + trims | 943 → **923** | 2 424 → **2 398** | 35 879 → **34 484** | 3 469 → **3 272** | 550 → 551 | 2 554 → **2 418** |

The trims are `SQLITE_OMIT_DEPRECATED`, `SQLITE_OMIT_DECLTYPE` and
`SQLITE_OMIT_COMPILEOPTION_DIAGS`; none of the removed APIs is exposed by the
HolyPHP binding. The size effect is small (archive 1 710 854 → 1 702 824
bytes), so part of the few percent is probably code layout in the VDBE
dispatch loop - still reproducible in both orders.

Directly comparing the two builds (the configuration a user gets) in one
paired loop, 15 rounds, page cache equalised:

| Workload | baseline | final | change |
|---|---:|---:|---:|
| point | 983 | 902 | −8% |
| range | 2 452 | 2 336 | −5% |
| feed | 35 260 | 33 160 | −6% |
| insert | 3 301 | 3 133 | −5% |
| bt | 546 | 544 | −0.4% |
| prepare | 2 480 | 2 384 | −4% |

The final build, same paired loop as the baseline above (ns/op with the
malloc counts from the exact counter runs):

| Workload | ns/op | malloc/op | malloc bytes/op |
|---|---:|---:|---:|
| point | 902 | 1.00 | 163 |
| range | 2 336 | 0.00 | 0 |
| feed | 33 160 | 28.00 | 21 377 |
| insert | 3 133 | 0.05 | 215 |
| bt | 544 | 0.00 | 0 |
| prepare | 2 384 | 0.00 | 0 |

Malloc counts are unchanged: the remaining allocations are the JSON aggregate's
string buffers and the runtime's `hstr`, not SQLite bookkeeping.

### Rejected changes

| Change | Measurement | Why rejected |
|---|---|---|
| Lookaside 1200 × 400 instead of the default 1200 × 40 | feed: 28 → 28 malloc/op, ns unchanged (absolute numbers were taken before the benchmark moved to `unix-excl`, so they are higher; the count is what matters) | +480 KiB per connection for nothing; the default already covers every small object. Turning lookaside *off* does hurt (28 → 119 malloc/op), so the default is worth keeping. |
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
