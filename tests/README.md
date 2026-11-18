# Tests

One entry point:

```bash
bash tests/run.sh            # positive + negative + docs
bash tests/run.sh manual     # tests that open a window or bind a port
```

| Folder | Rule |
|---|---|
| `positive/` | must compile **and** run to completion (exit 0) |
| `negative/` | must be **rejected** by the type/borrow checker |
| `manual/` | needs a screen (`ui_selftest`), a loopback port (`ws_selftest`), or threads (`thread_lib`); run on demand |
| `check_docs.sh` | every snippet in `docs/*.html` that imports a library must pass `hphp check` |

`check_docs.sh` exits 77 when no Python interpreter is available; `run.sh`
reports that as SKIP, never as PASS.

## Adding a test

A positive test is a `.hphp` file that prints its own `OK`/`FAIL` lines and
exits non-zero on failure — see `positive/ide_workbench.hphp`. Keep tests
headless: if a test needs a window, it belongs in `manual/`.
