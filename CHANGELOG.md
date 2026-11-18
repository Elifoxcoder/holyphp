# Changelog

What changed for people who use HolyPHP. One section per released version,
newest first. Work that is not released yet goes under `## Unreleased`;
`scripts/ship.sh` renames that heading to the version it releases.

Write entries for users, not for the commit log: what they can do now, what
behaves differently, what they have to change.

## Unreleased

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

## 1.0.2

Recorded after the fact; this is the last version before the changelog was
introduced.

- UI library 2.0: one theme, no coordinates, text measured in logical pixels,
  scrolling windows.
- Chromium (WebView2) can be hosted in a HolyPHP window.
- Threading and async libraries; imports without a file extension.
- Release pipeline fixes: stale installer payload, registry URL, version skew;
  winget manifests at spec 1.12.0.