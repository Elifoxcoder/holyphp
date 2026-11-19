# Changelog

What changed for people who use HolyPHP. One section per released version,
newest first. Work that is not released yet goes under `## Unreleased`;
`scripts/ship.sh` renames that heading to the version it releases.

Write entries for users, not for the commit log: what they can do now, what
behaves differently, what they have to change.

## Unreleased

- SQLite 3.46.1 is now built into the compiler: the pinned amalgamation is
  vendored under `third_party/sqlite/` (public domain, checksums and build
  options recorded there), compiled once per platform with the B-tree driver,
  and linked only into programs that call `sqlite_*` or `bt_*`. Both builtin
  families therefore work from a clean checkout on Linux and Windows with no
  system SQLite and no network access; `HPHP_SQLITE_LIB` still substitutes
  another archive. See `docs/lib-sqlite.html`.
- New `tests/positive/bt_cursor.hphp` exercises the B-tree cursor builtins
  (seek, record decoding, index counts, batched tail) against a 36 KiB
  committed fixture database.
- New `bash benchmarks/challenge/run.sh` builds the twelve-dollar challenge
  server with this compiler and runs its 42 acceptance checks in direct
  (io_uring) and classic mode on a small committed copy of the seed database.
  Both modes pass, so the SQLite and io_uring runtime modules are checked
  against a real application from a clean checkout.
- `sqlite_status()` now really returns its JSON text (its declaration said
  `int`), and `bt_read()` / `bt_count_range()` return the integer count /
  status they produce instead of being declared `bool`.
- Builtin ownership audit, regex: every `preg_*` call now frees its parsed
  pattern tree and (for `preg_match_all`) the per-match row, `preg_replace()`
  releases its output buffer, and `preg_split()` returns a copy of the subject
  when the pattern is invalid instead of aliasing it. A 20000-round regex loop
  stays flat.
- Builtin ownership audit, file/io: `file_get_contents()` releases its read
  buffer, `basename()` releases the intermediate string when it strips a
  suffix, `pathinfo()`/`is_writable()` no longer leave temporary strings
  behind, and `exec()` works again (the builtin had a two-argument C prototype
  but no implementation; both `exec($cmd)` and `exec($cmd, $out)` now run).
- Builtin ownership audit, JSON: the pretty printer returns a string whose
  intermediate buffer is released (it leaked the whole compact document per
  call), and the fast field readers are documented as borrowing the JSON text
  and returning a fresh value. `json_encode()`/`json_decode()` results belong
  to the caller.
- Builtin ownership audit, math: `srand()`/`mt_srand()` now really reset the
  sequence (the one-time time seed used to overwrite the first user seed),
  `lcg_value()` works (the builtin had no implementation, so any program using
  it failed to link), and `atan2()` accepts its documented two arguments
  instead of being declared as a one-argument builtin. Math builtins borrow
  their numbers and allocate nothing.

- Builtin ownership audit, arrays: `shuffle()` now mutates the array and
  returns `true` like PHP (it used to build a new array that the declared
  `bool` return threw away), and `array_splice()` removes the requested range
  from the source array instead of returning a copy and leaving the array
  unchanged. The array read builtins (`end`, `reset`, `max`, `min`,
  `array_search`, `array_key_*`) are documented as borrowing their array and
  elements.
- Builtin ownership audit, strings: `str_pad()` always returns a new string
  (it used to return its argument when no padding was needed), `strval()`,
  `ucfirst()`/`lcfirst()` on an empty string, `wordwrap(..., 0)` and
  `preg_replace()` with an invalid pattern now return copies instead of the
  caller's value. `str_replace()`, `nl2br()`, `htmlentities()`,
  `html_entity_decode()`, `strip_tags()`, `bin2hex()`, `hex2bin()`,
  `urlencode()`, `urldecode()`, `sprintf()`, `implode()` and `wordwrap()`
  build their result once and release the temporary buffer, so a 20000-round
  string loop no longer grows. `tests/rss.ps1` / `tests/rss.sh` check that.

- Every builtin that hands out a raw pointer, an opaque handle it later frees,
  or skips a bounds/type/initialisation check now carries the `unsafe` flag and
  says so on its documentation page: `str_free`, `arr_free`, `object_free`,
  `iarr_release`, `iarr_new_uninit`, `sb_discard`, `scope_end`,
  `bt_multi_payloads`, `bt_multi_release`, `bt_page_release`,
  `bt_payload_pin` and `bt_tail` joined the raw-memory group (42 flagged
  builtins in total). Safe code that calls one of them must wrap it in
  `unsafe { }`; the challenge benchmark already did so for the B-tree ones.
  `scripts/check-repo.sh` now fails when a `*_ptr`/`*_fast` builtin is not
  flagged or a flagged builtin's description does not mention `unsafe`.

- Ordinary `new Class()` expressions now have the normal copyable class type; explicit `own Class` slots can still take a fresh instance.
- By-reference calls to mutating builtins are checked as one write, and separate scalar by-reference arguments no longer conflict merely because their storage is boxed.
- Closure escape diagnostics now distinguish values actually passed to a retaining callback from unrelated heap values, while keeping conservative borrow checks for unknown callbacks.
- The runtime now reports the same version as the compiler (`phpversion()`
  said 1.0.0 while `hphp --version` said 1.0.2).
- `tests/run.sh --asan` runs every test under AddressSanitizer,
  UndefinedBehaviorSanitizer and LeakSanitizer (Linux/WSL); the build side is
  `scripts/build-asan.sh`. The findings at the start of the memory-safety work
  are listed in `STATUS.md`.
- The io_uring/epoll transport and the SQLite binding are optional runtime
  modules now (`hphp_uring.c`, `hphp_sqlite.c`); the compiler links them only
  when a program calls one of their builtins. On Linux the backend links a
  static SQLite with the B-tree driver when `HPHP_SQLITE_LIB` is set, and the
  system library otherwise.
- New builtins: string builders (`sb_*`), typed integer arrays (`iarr_*`),
  request-scope compatibility (`scope_begin`/`scope_end`/`str_persist`),
  byte/word memory accessors (`u8_at`, `i64_at`, …), HTTP/JWT/JSON helpers,
  and the full `sqlite_*`/`bt_*`/`epoll_*`/`stream_*` transport set.
- `password_hash()` no longer writes one byte past a stack buffer while
  encoding its salt (found by the sanitizer suite; `password_verify()` accepts
  the hashes it produces as before).
- Programs that never use SQLite or `io_uring` no longer link `libsqlite3` or
  `liburing`. The two benchmark-only HTTP engines (`holy_maxout_start()`,
  `holy_challenge_start()`) are no longer builtins; their sources stay under
  `benchmarks/maxout/` with a build note.
- A file that ends inside `/* …` is now rejected with an "unterminated block
  comment" error instead of reading past the end of the source, and deeply
  nested expressions (thousands of `(`, `!`, `**` or `{`) are rejected with
  "nesting too deep" instead of overflowing the parser's stack.
- New: `bash tests/fuzz/run.sh` builds and runs libFuzzer targets for the
  compiler front end and the string/array/JSON builtins (see
  `tests/fuzz/README.md`).

## 1.0.2

Recorded after the fact; this is the last version before the changelog was
introduced.

- UI library 2.0: one theme, no coordinates, text measured in logical pixels,
  scrolling windows.
- Chromium (WebView2) can be hosted in a HolyPHP window.
- Threading and async libraries; imports without a file extension.
- Release pipeline fixes: stale installer payload, registry URL, version skew;
  winget manifests at spec 1.12.0.