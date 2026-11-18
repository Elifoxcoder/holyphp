# Fuzzing

Two [libFuzzer](https://llvm.org/docs/LibFuzzer/) targets under ASan + UBSan:

| Target | Binary | What it feeds |
|---|---|---|
| `compiler` | `build/fuzz/fuzz_frontend` | arbitrary bytes to the lexer, parser and `sema` |
| `runtime` | `build/fuzz/fuzz_runtime` | string, array, JSON, encoding and hashing builtins |

Neither target compiles or runs a HolyPHP program. The compiler target stops
after `sema_run()`; the runtime target calls `hpbi_*` functions directly with
values built from the input. Imports, code generation and the C compiler are
covered by the normal test suite instead.

## Run

Needs Linux and clang with the libFuzzer runtime (Ubuntu:
`apt-get install clang`). From the repo root:

```sh
bash tests/fuzz/run.sh                  # both targets, 20 minutes each
bash tests/fuzz/run.sh compiler 60      # one target, 60 seconds
bash tests/fuzz/run.sh runtime 600
```

`run.sh` builds the targets, runs them with `-fork=4` (override with
`FUZZ_FORK`), saves sanitizer output to `tests/fuzz/logs/<target>.log` and
crash inputs to `tests/fuzz/artifacts/<target>/`. `tests/fuzz/corpus/<target>/`
holds the curated seeds; the fuzzer's growing corpus lives in
`build/fuzz/corpus/<target>/` so it does not flood git.

Both targets run with `detect_leaks=0`: the compiler keeps its AST until exit
(STATUS.md F1) and the runtime has no reference counting yet (F2). Those are
known and tracked; they would otherwise stop every run on the first input.

UBSan does not abort in these runs (`halt_on_error=0`) because a known
`hp_arr_sum` overflow (see `known.txt`, QUESTION 1 in
`.agent/REPORT-opencode-2.md`) is in a file this work may not edit. `run.sh`
greps the log for `runtime error:` / `ERROR: AddressSanitizer` and fails on
anything not listed in `known.txt`. Remove an entry as soon as it is fixed.

## What a target must tolerate

Invalid programs are the normal input for the compiler target. The lexer
reports many syntax errors through `fatal()`, which exits in a normal compiler
run; the fuzz harness installs `hp_fatal_jmp` (util.c) so such an error unwinds
to the fuzzer instead of ending the run. Fuzzer findings in the real compiler
are therefore crashes, sanitizer reports and hangs — not "it rejected my
input".

The runtime harness catches `hp_throw` with a `hp_try_frame`, because throwing
is defined behaviour for bad arguments (empty separator, invalid number).

## Findings so far (2026-11-19)

A 20-minute run of each target (`-fork=2 -rss_limit_mb=1024`) executed
562 876 inputs on the compiler target and 30 438 216 on the runtime target.

- `heap-buffer-overflow` in `lexer.c:adv` on a file ending inside `/* …`:
  `skip_trivia()` advanced twice after reaching EOF. Fixed in `lexer.c`; the
  regression test is `tests/negative/unterminated_comment.hphp`.
- `stack-overflow` in `parse_binop_level` (`parser.c`) on deeply nested
  expressions (`((((…`, `!!!!…`, `1**1**…`). Fixed with a 400-level parser
  depth limit and `tests/negative/nesting_too_deep.hphp`.
- `signed integer overflow` in `hp_arr_sum` (`runtime/hphp_rt.c:1033`) when the
  array contains huge numeric strings (`"9223372036854775807"`,
  `"11111111111111111"`). Not fixed here: `runtime/hphp_rt.c` belongs to the
  memory-core agent. Listed in `known.txt` (QUESTION 1).
- `SEGV` in `sema.c` (1783/1823/3322): `check_bin`/`check_expr` dereference a
  null `Type` while checking an invalid program. Not fixed here: `sema.c`
  belongs to the memory-core agent. Minimal reproducer:
  `tests/fuzz/repro/sema-null-type.hphp`; listed in `known.txt` (QUESTION 2).

After both fixes the compiler target only finds the known `sema.c:1783` crash;
the runtime target finds no sanitizer error. The runtime log's OOM entries are
the known F2 leaks, restarted by `-fork`; `slow-unit-*` inputs stay below the
25 s timeout.
