# scratch/

Loose working files that used to sit at the repository root. Nothing here is
part of the compiler, the runtime, the libraries or the documented workflow —
it is kept for reference, not because the build needs it.

The two server entry points are still the canonical programs used for the
benchmarks in `../README.md`; they just live here now so the root stays
readable.

| File | What it is |
|---|---|
| `server.hphp` | MaxOut benchmark server (drives `holy_maxout_start`) |
| `server_challenge.hphp` | twelve-dollar-server-challenge entry point (drives `holy_challenge_start`) |
| `test_hello.hphp`, `test_vps.hphp` | Three-line smoke programs used to check a fresh build |
| `test_c.c` | Host check: does `io_uring_queue_init` / SQLite link and work here |
| `build-win.sh` | Builds `hphp.exe` with MSYS2/UCRT64 MinGW; run from the repo root |
| `main.rs`, `Cargo.toml`, `Cargo.lock` | Rust baseline used for the head-to-head comparison |
| `append_std.py`, `update_std.py`, `fix_std.py`, `patch_hphp.py`, `patch_main.py`, `patch_rt.py` | One-off codemods that pasted / repaired the engines inside `src/hphpc/runtime/hphp_std.c`. Already applied; they are here for provenance. Run them from the repo root. |
| `holyphp_src.tar.gz` | Snapshot of the sources taken on the benchmark VPS |

Build them from the repository root, e.g.:

```bash
bash scripts/build.sh
./hphp build scratch/server.hphp -o /tmp/holy_server
```

`build-win.sh` walks up one directory, so it can be run from anywhere:

```bash
bash scratch/build-win.sh
```
