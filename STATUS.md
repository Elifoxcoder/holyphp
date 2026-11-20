# Status

_Last updated: 2026-11-19._

Two questions this file answers: what state the repository is in, and how far
HolyIDE is from being an editor you could use for real work.

---

## 1. Repository state

The tree was reorganised so that each kind of file has exactly one home and one
source of truth.

| Area | Before | Now |
|---|---|---|
| Libraries | flat `lib/ui.hphp` **and** a drifting copy in `lib/ui/ui.hphp`; `html`/`ide` only on disk | one package folder per library: `lib/<name>/hphp.json` + `<name>.hphp` (+ parts). All six are embedded — `import "html"` / `import "ide"` work with an installed compiler in a project that has no `lib/` at all |
| Tests | `tests/positive/run_all.sh`, a docs checker hidden in `positive/`, self-tests in `examples/` | `tests/run.sh` (positive + negative + docs), `tests/manual/` for window/socket/thread tests, `tests/check_docs.sh` |
| Examples | 30 files: probes, generated C (71 KB), three `.exe`s, a German duplicate tour, a game | 13 curated programs + `bench`/`record` C twins |
| Docs | 12 hand-written pages, stale counts (235 builtins, 5 libraries), examples of deleted files | one site in `docs/`, new `html` and `ide` pages, current counts, `examples.html` matches the tree |
| Junk | CEF profile (`holybrowser/cefdata`), `build/`, crash probes (`lib/probe_*.hphp`), six one-off `fix_examples*.py`, `registry/server.log` | removed; `.gitignore` covers browser profiles, build output and logs |

Build output stays out of the tree: `build/`, `*.exe`, `holybrowser/cefdata/`.

### Verified

```
bash scripts/build.sh              → build ok: hphp.exe (runtime embedded)
bash tests/run.sh                  → 29 passed, 0 failed, 0 skipped
bash tests/run.sh manual           → window / socket / thread self-tests
./holyide/holyide.exe --selftest   → toolbar buttons are boxes, pane border,
                                     no narrow text rect, sidebar bg, status bg,
                                     title bg, bottom: ok
./build/holyhtml_demo.exe          → pixels ok, frames=60 paints=61 relayouts=62
```

`tests/run.sh` runs 19 positive programs, 9 negative ones and 21 documentation
snippets. `tests/positive/ide_workbench.hphp` alone makes 167 assertions and
`ide_editor.hphp` 76, out of 393 in the positive tests.

### The first real session, and what it broke

HolyIDE was opened on a 125 % display and did not survive contact: no
shortcut worked, no file opened, every label was cut short, the terminal
ignored the keyboard, pressing a letter typed its ASCII code, panes could not
be resized and nothing was selected with the mouse. Ten symptoms, four root
causes, all of them now covered by a test:

- **An inline span inside a flex container was not blockified.** The whole
  title bar, the status bar and every tab's close glyph were merged into one
  run of anonymous text: no boxes, so no padding, no background, no click
  target and no ellipsis to stop at. That is why no button could be clicked
  and why labels were cut off (`layout.hphp`, `build_flex`).
- **A key press delivered its code point as a number.** The handler is written
  `key($vk, $ch, $mods)` and inserts `$ch`, so typing `a` inserted "97". The
  character now travels as a string, and Ctrl/Alt combinations send an empty
  one - except Ctrl+Alt, which is AltGr and has to keep typing `@` and `{`
  (`hphp_ui.c`, `cv_fire_key`).
- **`position: absolute` children were never measured.** Every popup - the
  palette, the completion list - was a zero-by-zero box that painted nothing,
  which is why `Ctrl+P` looked like a key that did nothing. Out-of-flow boxes
  are sized now and positioned by `place_abs()` (`layout.hphp`).
- **Text widths were rounded to nearest while the drawing rectangle was
  truncated.** At 125 % a string measured 31.25 device pixels, the rect got 31,
  and GDI dropped the last glyph - "Save" was rendered "Sav" everywhere. Widths
  round up now, a plain draw gets one pixel of slack, and the painter no longer
  clamps a line's last atom to "whatever is left of the box".

Two more fell out of the same session: the stylesheet was cached while it
contains the pane sizes and the font size (so a dragged grip laid the document
out at the old size), and the wheel moved three lines a notch, which is not
scrolling but jumping.

### Fixes that fell out of the cleanup

Things that were broken and would have blocked HolyIDE:

- **Libraries could not be imported together.** `ui` and `html` both defined a
  class `Layout`; a program has one namespace, so an HTML view inside a `ui`
  window silently used the wrong one. The layout engine is `HtmlLayout` now.
- **`import "html"` / `import "ide"` failed on an installed compiler**, because
  only `lib/*.hphp` was embedded and the resolver only knew flat libraries.
  Embedding now keeps the path inside `lib/` (`html/dom.hphp`), the resolver
  looks in `lib/<name>/<name>.hphp`, and `hphp pkg install <lib>` stages the
  whole folder, not just the entry file.
- **`flex-basis` was parsed and ignored.** `flex: 1` started each item at its
  content size instead of 0, so a column with 1 px too much content pushed its
  last child off-screen (the IDE's status bar sat one pixel below the window).
  Basis is honoured now, and a box that receives a height from its flex parent
  re-runs its own distribution.
- **A window resize left the layout stale.** The workbench only re-parsed the
  document when its HTML changed, never when the viewport did. It now caches
  the size along with the document.
- **Two logical-pixel conversions disagreed** (`cv_unscale` truncated,
  `ui_client_size` rounded), so a canvas reported 784 px in a 783 px window at
  non-100% DPI.

---

## 2. How close is HolyIDE?

`holyide/holyide.hphp` runs. Explorer, tabs, an editor with undo/redo and
completion, a command palette, a terminal with the runner, a problems list, a
status bar, resizing and a caret all work, and the window app has a pixel
self-test.

**Distance to a usable daily driver for HolyPHP: roughly 70%.**
**Distance to a general-purpose IDE: roughly 35%** — the missing half is the
ecosystem around the editor (search, git, settings, extensions, debugging).

### Done

| Area | State |
|---|---|
| Text editing | insert/delete, newline with auto-indent, backspace that joins lines, Tab/Shift+Tab, Alt+↑/↓ move, Ctrl+D duplicate, Ctrl+/ comment, word/page/doc movement, bracket matching |
| Selection | shift+arrows, select-all, mouse click and drag, double-click for a word, gutter click for a line, `first_end`/`last_end`, cut/copy/paste through the system clipboard |
| History | line-slice undo/redo, coalesced per typing run, 500 steps |
| Syntax | line tokenizer (keywords, types, functions, variables, strings, numbers, comments), `/* */` carried across lines |
| Completion | vocabulary from builtins + file words, 2-character threshold, `Ctrl+Space` on demand, popup with keyboard and mouse selection |
| Files | explorer that folds (folders, 800-entry/6-level cap, build dirs skipped), tabs with dirty dot and a clickable close, save, save-all, revert, new untitled buffer |
| Navigation | Ctrl+P files, Ctrl+Shift+P commands, Ctrl+G line, click to caret, one line a notch with the wheel, tree and code scrolling with their own step |
| Running | `hphp run <file>` in the file's folder, live stdout/stderr in the terminal panel, exit code, problem parsing with click-to-open, and a terminal command line (Ctrl + backtick) that runs through `cmd /c` with history |
| Chrome | title bar with run/stop, panel tabs (terminal/problems), status bar, zoom, and draggable pane borders (explorer and panel) |
| Testing | 393 headless assertions plus a canvas pixel self-check that also proves the toolbar has boxes and no string is drawn too narrow |

### Missing (in rough priority order)

1. **Diagnostics while typing.** Problems only appear after a run. Needs a
   debounced background `hphp check`, cancellation of the previous one, and
   squiggles/gutter marks.
2. **Search and replace across files.** A project-wide find is the single
   biggest gap for daily use; `ide_scan_tree` already gives the file list.
3. **Session persistence.** Window size, open folder, tabs, caret positions and
   unsaved buffers die with the process.
4. **Save-as / rename / delete / new file from the explorer.** The tree is
   read-only; there is no dialog for a path without one.
5. **File watching.** External edits are only noticed on revert.
6. **Tab overflow.** More tabs than fit in the strip run off the edge; a scroll
   or an overflow menu is needed.
7. **Split editors.** One editor per window; the pane borders drag, the editor
   itself does not split.
8. **Git.** No status decoration, diff view, branch display, or commit — not
   even plumbing.
9. **Settings.** No file, no UI; font size and theme are code constants.
10. **Theming.** Only `html_theme_dark()` is wired; a light theme exists in the
    library but the app does not switch to it.
11. **Multi-cursor, column selection, code folding** — quality-of-life, none of
    it started.
12. **Debugger, extensions, LSP, tasks** — the ecosystem layer. The panel's
    command line runs a command and shows its output; it is a pipe, not a TTY
    (no interactive programs, no colours, no cursor addressing).
13. **Packaging.** No app icon, no installer entry, no "open with" integration,
    no file association for `.hphp`.

### Roadmap

**0.2 — daily driver** (the first four gaps): background `hphp check` with
squiggles, project search/replace, session restore, save-as/rename/delete, file
watching, tab overflow.
**0.3 — workspace**: split editors, settings file + UI, light theme, git status
in the status bar and a read-only diff. (Draggable panes are done.)
**1.0 — beyond the editor**: debugger (breakpoints over a `hphp` debug build),
an extension point, and an installer that registers `.hphp`.

Each milestone should keep the current rule: the model and the chrome are a
pure state machine rendered to HTML, tested headlessly; the window stays a thin
shell.

---

## 3. Known limitations that are not IDE work

- **Argument evaluation order.** Generated C evaluates call arguments
  right-to-left, so `f() . count($x)` may read `$x` before `f()` runs. PHP is
  left-to-right. Not fixed; avoid relying on side-effect order inside one
  expression.
- **Windows-only desktop.** `ui`, `html` painting and HolyIDE need Win32. The
  libraries still type-check and the headless halves still run on Linux, and
  `hphp` builds there (the UI layer compiles to inert stubs).
- **`flex-shrink` is not implemented.** A toolbar or a status bar whose items
  are wider than the window still runs off the right edge and is clipped, where
  a browser would shrink the items down to their minimum content. Grow is
  distributed; shrink is not.
- **Scrolling is per line, not per pixel.** The pane scrolls one row a notch,
  which is smooth for reading and is what a text pane does; there is no
  fractional scroll offset, so a trackpad's fine-grained scroll arrives as whole
  lines.
- **Documentation snippets** are checked by `tests/check_docs.sh` only when a
  Python interpreter is available; without one the suite reports SKIP.

---

## 4. Sanitizer findings (2026-11-18)

`tests/run.sh --asan` runs the whole suite under AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer: the compiler is built as
`hphp_asan` (`scripts/build-asan.sh`, output `build/hphp_asan`), and every
backend `gcc` call is routed
through a wrapper that adds the same flags, because the runtime is embedded as
C source and compiled per program. Per-test output lands in
`build/asan-logs/<test>.log`. Sanitizers need Linux; this ran in
`wsl -d Ubuntu-24.04 -u root`.

Current result on `safe/phase0` (2026-11-19): **54 test runs with findings**,
1 passed. F3 (below) is fixed; the remaining findings are the compiler and
program leaks (F1/F2) and the known `ide_workbench` defect (F4). The normal
(non-sanitizer) suite at the same commit has two unrelated pre-existing
failures: `ide_workbench.hphp` (4 assertions, crashes the same way as F4) and
a docs snippet `pkg_02.hphp` importing `mathx` (library not in this
checkout).

### F1 — LeakSanitizer, compiler process (all 50 runs)

Every `hphp_asan` invocation leaks between ~2 KB and ~0.8 MB (20 to 32 000
allocations). The compiler frees nothing at exit: tokens, AST, types and the
codegen text buffers are all kept. Representative stack (`check` on a
negative test):

```
==2253==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 80 byte(s) in 1 object(s):
    #0 calloc
    #1 xcalloc src/hphpc/util.c:16
    #2 main src/hphpc/main.c:1594
Direct leak of 64 byte(s) in 1 object(s):
    #1 xrealloc src/hphpc/util.c:11
    #2 ptrvec_push src/hphpc/util.c:62
    #3 load_source_recursive src/hphpc/main.c:1113
Direct leak of 392 byte(s) in 1 object(s):
    #2 expr_new src/hphpc/ast.c:4
    #3 parse_primary src/hphpc/parser.c:1472
SUMMARY: AddressSanitizer: 2179 byte(s) leaked in 20 allocation(s).
```

Aggregated allocation sites: `xmalloc src/hphpc/util.c:6` (mostly `fmt`,
17 095 objects), `xrealloc src/hphpc/util.c:11` (7 492), `xcalloc
src/hphpc/util.c:16` (1 535), `buf_grow src/hphpc/util.c:72` via `buf_printf
src/hphpc/util.c:99` from `emit_fn`/`codegen_emit` (largest single run: 476 KB
for `html_css`).

### F2 — LeakSanitizer, generated program (29/29 positive tests)

Every positive test leaves its runtime values behind: strings
(`hp_str_new` `rt/hphp_rt.c:103`, `hp_str_concat_n` `:261`, `hp_str_join_v`
`:230`), arrays (`hp_arr_new` `:551`, `hp_arr_grow` `:561/:563`,
`hp_arr_set` `:919`), boxes (`hp_box_new` `:1385`) and argv
(`hp_argv_from_c` `:138`). This is the known “no reference counting yet” state,
not a new bug — but it is the exact list that phase 1 has to remove.

Representative stack (`hello.hphp`, 497 B in 11 allocations):

```
==1098==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 41 byte(s):
    #1 hp_alloc rt/hphp_rt.c:34
    #2 hp_str_concat_n rt/hphp_rt.c:261
    #3 hp_main prog_4286301e2.c:41
```

Per test, program-side leaks:

| Test | Leaked |
|---|---|
| borrow_builtin_mutation.hphp | 931 B in 16 allocations |
| borrow_closure_unrelated.hphp | 916 B in 22 |
| borrow_new_object.hphp | 344 B in 7 |
| byref.hphp | 522 B in 13 |
| byref_storage.hphp | 7 404 B in 171 |
| codegen_regress.hphp | 1 618 B in 25 |
| concat_many.hphp | 4 257 B in 141 |
| controlflow.hphp | 10 805 B in 282 |
| early_return.hphp | 3 050 B in 55 |
| explode_chars.hphp | 2 140 B in 43 |
| features.hphp | 3 849 B in 94 |
| globals.hphp | 1 026 B in 28 |
| globals2.hphp | 1 082 B in 23 |
| hello.hphp | 497 B in 11 |
| html_css.hphp | 706 424 B in 17 975 |
| html_layout.hphp | 5 926 688 B in 226 449 |
| ide_editor.hphp | 355 067 B in 7 508 |
| ide_workbench.hphp | 360 689 B in 34 862 |
| match_narrow.hphp | 7 995 B in 190 |
| mixedops.hphp | 9 791 B in 169 |
| modern8.hphp | 28 144 B in 492 |
| modules.hphp | 412 425 B in 2 343 |
| password_hash.hphp | 1 735 B in 30 |
| nullable.hphp | 8 059 B in 171 |
| nullobj.hphp | 339 B in 10 |
| regress.hphp | 14 376 B in 301 |
| strict.hphp | 9 221 B in 188 |
| switch_narrow.hphp | 4 368 B in 96 |
| unsafe_memory_builtins.hphp | 305 B in 4 |
| values.hphp | 13 390 B in 255 |

### F3 — fixed: AddressSanitizer stack-buffer-overflow in `hp_b64_encode_raw`

`tests/positive/modules.hphp` (`password_hash`, generated C line 91):

```
==1568==ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 1
    #0 hp_b64_encode_raw rt/hphp_std.c:4199
    #1 hpbi_password_hash rt/hphp_std.c:4212
    #2 hp_main prog_5feaf63d8.c:91
  'b64salt' (line 4211) <== Memory access at offset 104 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow rt/hphp_std.c:4199
```

`hpbi_password_hash` declared `char b64salt[24]`; base64 of a 16-byte salt is
24 characters **plus** the terminator, so the encoder wrote one byte past the
array. Fixed: `b64salt[25]`, `b64dk[45]`, `b64dk2[45]`. Regression test:
`tests/positive/password_hash.hphp` (format, round-trips, long/empty
passwords, malformed hashes). The 2026-11-19 sanitizer run no longer reports a
stack-buffer-overflow; `password_hash.hphp` only shows the F2 leaks every
program has.

### F4 — UBSan + ASan: null object access and SEGV in `ide_workbench`

`tests/positive/ide_workbench.hphp` (the same test that already fails without
sanitizers):

```
prog_4e9a1bb5a.c:12799:15: runtime error: member access within null pointer
    of type 'struct IdeProblem'
    #0 hp_main prog_4e9a1bb5a.c:12799
AddressSanitizer:DEADLYSIGNAL
==1291==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000008
    (READ)
    #0 hp_main prog_4e9a1bb5a.c:12799
```

The test dereferences a null `IdeProblem` while reading the problems panel
after a failing run. The preceding assertions fail because the problems list
stays empty, so the test logic and the crash are the same defect.

### Not found

No use-after-free, no double free, no uninitialised read, and no UBSan report
outside F4. The only memory *error* in the whole suite was F3; everything else
is a leak (F1/F2) or the known `ide_workbench` defect (F4).

---

## 5. Fuzzing results (2026-11-19)

`bash tests/fuzz/run.sh` builds two libFuzzer targets (clang 18, ASan + UBSan)
and runs them for 20 minutes each; see `tests/fuzz/README.md`. Both were run
in WSL on `safe/phase0`, in parallel, with `-fork=2 -rss_limit_mb=1024`
(the fork restarts are the known F1/F2 leaks).

| Target | Time | Executions | Corpus | Sanitizer findings |
|---|---|---|---|---|
| compiler front end | 1219 s | 562 876 | 1477 | 18 × the known `sema.c:1783` null-Type SEGV |
| runtime builtins | 1207 s | 30 438 216 | 936 | none |

Fixed while fuzzing:

- **Stack overflow in the parser** (`parse_binop_level`, found on 1.8–5.4 KB
  inputs): deeply nested `((((…`, `!!!!…`, `1**1**…` or `{{{…` exhausted the
  stack. The parser now stops at 400 levels with
  `error: nesting too deep (limit 400)`; regression test
  `tests/negative/nesting_too_deep.hphp`.
- **Read past the source buffer** (`lexer.c:adv`): a file ending inside
  `/* …` made `skip_trivia` advance twice after EOF. Fixed; the regression
  test is `tests/negative/unterminated_comment.hphp`.

Open, in files this work may not edit:

- `sema.c:1783` (also 1823/3322): `check_bin` dereferences a null `Type` for
  invalid input. Minimal reproducer: `tests/fuzz/repro/sema-null-type.hphp`
  (`k(PHP_I==r)a`).
- `hphp_rt.c:1033`: `hp_arr_sum` overflows `int64_t` on arrays holding huge
  numeric strings. Both are listed in `tests/fuzz/known.txt` so the fuzzer
  keeps looking for new bugs instead of stopping on them.

OOM and `slow-unit` artifacts in the runtime log are the known leaks (F2) and
inputs that take longer than usual, not memory errors.

---

## 6. Vendored SQLite and the challenge benchmark (2026-11-19)

SQLite is no longer an operator-provided dependency. `third_party/sqlite/`
holds the 3.46.1 amalgamation (`sqlite3.c`, `sqlite3.h`, `sqlite3ext.h`) with
its provenance, compile options and published hashes in its README:

```
archive  sqlite-amalgamation-3460100.zip  SHA3-256 af6aae8d…d1e288
sqlite3.c                                 SHA3-256 186a1baa…0a7dad
```

The zip hash matches sqlite.org's download page for the 2024-08-13 release;
the file hash matches the release log. `scripts/build.sh` concatenates
`sqlite3.c` with `src/hp_btree.c` (the B-tree builtins use SQLite's private
structures, so a system library cannot provide them), compiles it once per
platform with the challenge's options into
`build/sqlite/<platform>/libsqlite3.a`, and embeds the archive and
`sqlite3.h` in the compiler as a generated `sqlite_embed.h`. At program link
time only a program that calls `sqlite_*` or `bt_*` gets the archive; on Linux
the result is a static link with no `libsqlite3.so` in `NEEDED`. Setting
`HPHP_SQLITE_LIB` still overrides the embedded archive.

Verification on this branch:

| Check | Windows | Linux (WSL, ELF compiler) |
|---|---|---|
| `scripts/build.sh` | build ok, 3.9 MB compiler | build ok, 4.0 MB compiler |
| `tests/positive/bt_cursor.hphp` | PASS (60 checks) | PASS |
| plain `examples/hello.hphp` | PASS, no SQLite | PASS, `NEEDED` only libm/libc |
| full `tests/run.sh` | 56 passed, 1 failed (VS Code LSP test, pre-existing), 0 skipped | 54 passed, 2 failed (`ide_workbench`, docs `pkg_02`/`mathx`, both pre-existing), 1 skipped (no node) |

The challenge suite now lives in the repository:
`bash benchmarks/challenge/run.sh [both|direct|classic]` builds
`benchmarks/challenge/server.hphp`, starts it on a fresh copy of
`benchmarks/challenge/fixture/feed.db` (36 KiB: the 22 posts the golden files
read, their 23 authors and 98 likes, cut from the deterministic seed by
`fixture/make-fixture.py`), and runs the bundle's 42 checks. Result:
**42/42 in direct mode and 42/42 in classic mode**. The server source is
adapted to this compiler only mechanically (unsafe blocks around raw-pointer
builtins, `u8_at`/`i64_at` instead of `*p`, definite initialisation); no
query, algorithm or response changed, and the original golden files still
match.

Known limits of the port: the background WAL checkpoint worker is
POSIX-only (the `sqlite_bg_checkpoint_*` calls report `false` on Windows,
everything else works), and direct io_uring mode is Linux-only (the runtime
falls back to the classic real-fd path on Windows and when `HPHP_DIRECT=0`).

Sanitizer check: `build/hphp_asan` must be rebuilt after a compiler change
(`tests/run.sh --asan` reuses an existing binary, and an old one links the
system SQLite, which has no B-tree internals). With a fresh build,
`bt_cursor` passes under ASan + UBSan with no report other than the known
F1/F2 leaks. That run found the declaration mismatch fixed in the same
commit series: `bt_read`/`bt_count_range` were declared `bool` while returning
integers (UBSan: load of value 2 for `_Bool`), and `sqlite_status` was declared
`int` while returning JSON text.

---

## 7. Builtin ownership audit (2026-11-19)

`docs/guide/builtin-ownership.md` lists all 547 builtins with the ownership
contract of the result and the arguments. 65 new `ownership_*` positive tests
cover nine groups (strings, arrays, math, JSON, file/io, regex, date, net,
misc); each group has a 20000-round loop whose resident set is checked with
`tests/rss.ps1` (Windows) or `tests/rss.sh` (Linux). All nine loops stay flat.

The audit fixed, among others: `str_pad()` returning its argument, `strval()`
and empty `ucfirst()`/`lcfirst()` aliasing their input, `shuffle()` returning a
new array while declared `bool`, `array_splice()` not splicing, `srand()` being
overwritten by the first random draw, `lcg_value()` having no implementation,
`atan2()` being declared one-argument, `basename($p, $suffix)` leaking the
untrimmed string, `exec()` having no implementation, `file_get_contents()`
leaking its read buffer, the pretty JSON printer leaking its compact document,
the regex engine never freeing its pattern tree, and `stream_peer()` leaking
one string per call. The io_uring module also compiles on Windows again (its
stats printer referenced Linux-only counters) and the thread module is linked
for `atomic_*`/`chan_*`/`mutex_*`/`barrier_*`.

Sanitizer result on the final tree: `tests/run.sh --asan` reports 119
LeakSanitizer logs (the known F1 compiler and F2 language leaks), the known
`ide_workbench` F4 SEGV/UBSan report, and no new memory error (no
use-after-free, no double free, no bounds error) in any ownership test.

### SQLite speed work (task 4.4, 2026-11-19)

`benchmarks/sqlite/` measures the module's own hot path — point read by primary
key, 20-row index range, the challenge's JSON feed query, a small indexed
insert, a 20-row B-tree cursor walk and a bare `sqlite_prepare` — and counts
allocations per operation with an `LD_PRELOAD` malloc counter. It opens the
database with `unix-excl` like `challenge/start.sh`; with SQLite's default VFS
every read transaction pays three `fcntl` locks and a `fstat`, which doubles
the point-read time. Numbers and the paired A/B method are in
`benchmarks/RESULTS.md`.

Kept: the vendored SQLite is compiled with `-O3`, and its unused-feature list
gained `SQLITE_OMIT_DEPRECATED`, `SQLITE_OMIT_DECLTYPE` and
`SQLITE_OMIT_COMPILEOPTION_DIAGS` (none of those APIs is reachable from
HolyPHP). Paired runs give about 4-8% on point reads, range scans, inserts and
the JSON feed; the B-tree cursor path is unchanged. `scripts/build.sh` now
stamps the flags used for the archive so a flag change rebuilds it.

Rejected after measurement, each with the numbers in `benchmarks/RESULTS.md`:
a 480 KiB lookaside (no malloc removed; the 48 KiB default already covers every
small object, while disabling lookaside costs 28 → 119 malloc/op), a 512-page
bulk page-cache allocation (reads 2% slower, inserts unchanged), persistent
prepared statements (only the one-time prepare changes), a fixed MEMSYS5 heap
(no speed win, `SQLITE_NOMEM` risk), and "statement journal off" (the challenge
statements create no statement journal at all).

The remaining per-request allocations are not SQLite bookkeeping: the feed's 28
allocations per request are the JSON aggregate's string buffers, and the
`point`/`feed` counts also include the `hstr` that `sqlite_column_text` must
return. Reducing those needs the memory-model work (regions / reference
counting), not another SQLite knob.

Verification on this branch with the new archive: `bash tests/run.sh` gives the
same 54 passed and the same two pre-existing failures (`ide_workbench`, docs
snippet `pkg_02` importing `mathx`); `bash benchmarks/challenge/run.sh both`
passes 42/42 in direct and classic mode; `scripts/check-repo.sh` is clean.
`tests/run.sh --asan` was run in WSL: 54 tests with findings, 1 passed, the
same picture as section 4, and no stack frame in `hphp_sqlite.c`,
`hp_btree.c` or `sqlite3.c`.

