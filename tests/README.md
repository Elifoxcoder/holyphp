# Tests

One entry point:

```bash
bash tests/run.sh            # positive + negative + docs
bash tests/run.sh manual     # tests that open a window or bind a port
bash tests/run.sh --asan     # the whole suite with ASan + UBSan + LeakSanitizer
```

| Folder | Rule |
|---|---|
| `positive/` | must compile **and** run to completion (exit 0) |
| `negative/` | must be **rejected** by the type/borrow checker |
| `manual/` | needs a screen (`ui_selftest`), a loopback port (`ws_selftest`), or threads (`thread_lib`); run on demand |
| `fuzz/` | libFuzzer targets for the compiler front end and the runtime builtins; `bash tests/fuzz/run.sh` (see `fuzz/README.md`) |
| `check_docs.sh` | every snippet in `docs/*.html` that imports a library must pass `hphp check` |

`check_docs.sh` exits 77 when no Python interpreter is available; `run.sh`
reports that as SKIP, never as PASS.

## Memory / ownership loops

The `ownership_*_loop.hphp` tests and `positive/memory_rc.hphp` run 20000
iterations and release every value they create. Check that the resident set
stays flat with:

```bash
bash tests/rss.sh tests/positive/ownership_strings_loop.hphp "ownership strings loop ok"
# Windows:
powershell -ExecutionPolicy Bypass -File tests/rss.ps1 tests/positive/memory_rc.hphp "memory rc loops ok"
```

Both scripts build the test with the local compiler, sample the process every
few milliseconds and fail when the tail median grows more than 16 MiB over the
warm-up median. A literal or a temporary expression inside a loop is a fresh
heap object in this core (STATUS.md F2), so the loop tests hoist their literals.


## Adding a test

A positive test is a `.hphp` file that prints its own `OK`/`FAIL` lines and
exits non-zero on failure — see `positive/ide_workbench.hphp`. Keep tests
headless: if a test needs a window, it belongs in `manual/`.
