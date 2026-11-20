# SQLite 3.46.1 (vendored)

HolyPHP ships one fixed SQLite so that the `sqlite_*` and `bt_*` builtins
behave the same everywhere, from a clean checkout, with no system package and
no network access. The B-tree cursor builtins need SQLite's internals, so
`src/hp_btree.c` is compiled as part of the amalgamation (it is appended to
`sqlite3.c` by the build); a stock system library cannot provide them.

## Provenance

| | |
|---|---|
| Version | 3.46.1 (encoded `3460100`) |
| Release | 2024-08-13 |
| Archive | `sqlite-amalgamation-3460100.zip` (2,764,032 bytes) |
| Downloaded from | `https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip` |
| Files kept | `sqlite3.c`, `sqlite3.h`, `sqlite3ext.h` (the shell and the documentation are not needed) |
| License | Public domain (`sqlite3.h` header, https://www.sqlite.org/copyright.html) |

Checksums, as published by sqlite.org:

```
sqlite-amalgamation-3460100.zip  SHA3-256 af6aae8d3eccc608857c63cf56efbadc70da48b5c719446b353ed88dded1e288
sqlite3.c                        SHA3-256 186a1baa476b6d546de155160ca6d30ff7b7e6ee375f0bb6445e1a3d180a7dad
```

The zip hash is from the sqlite.org download page of 2024-08-15 (Wayback
Machine copy of `https://www.sqlite.org/download.html`); the `sqlite3.c` hash
is from `https://www.sqlite.org/releaselog/3_46_1.html`. The SHA-256 of the
zip, used by the challenge build script this vendor directory replaces, is
`77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784`; it matches
the archive these files came from.

To re-verify a checkout, with Python 3:

```sh
python3 - <<'EOF'
import hashlib
print(hashlib.sha3_256(open("third_party/sqlite/sqlite3.c", "rb").read()).hexdigest())
EOF
# 186a1baa476b6d546de155160ca6d30ff7b7e6ee375f0bb6445e1a3d180a7dad
```

`.gitattributes` keeps these files byte-for-byte (`-text`), so the hash above
holds after a checkout on any platform.

## How it is built

`scripts/build.sh` concatenates `sqlite3.c` and `src/hp_btree.c`, compiles the
result once into `build/sqlite/<platform>/libsqlite3.a` with the options below,
and embeds that archive into the compiler binary. At program link time the
compiler writes the archive to its build directory and links it whenever the
program uses a `sqlite_*` or `bt_*` builtin. `HPHP_SQLITE_LIB` overrides the
embedded archive with another one.

Compile-time options (the same list the challenge build used; they remove the
features this binding does not use and keep the layout the B-tree code
expects):

```
SQLITE_THREADSAFE=2            SQLITE_DEFAULT_MEMSTATUS=0
SQLITE_OMIT_PROGRESS_CALLBACK  SQLITE_OMIT_SHARED_CACHE
SQLITE_OMIT_LOAD_EXTENSION     SQLITE_OMIT_AUTOINIT
SQLITE_OMIT_UTF16              SQLITE_OMIT_TRACE
SQLITE_OMIT_COMPLETE           SQLITE_OMIT_GET_TABLE
SQLITE_OMIT_TCL_VARIABLE       SQLITE_USE_ALLOCA
HAVE_FDATASYNC=1               HAVE_USLEEP=1
```

`-fno-strict-aliasing` is required, as for every SQLite amalgamation build.

## Updating

Replace the three files with the ones from the new amalgamation, update the
version and checksums in this file and in `scripts/build.sh` /
`src/hphpc/main.c` if they are mentioned there, rebuild both platforms, and
run `tests/run.sh` plus `benchmarks/challenge/run.sh`. The B-tree code in
`src/hp_btree.c` uses private SQLite structures (`Btree`, `BtCursor`,
`MemPage`, `sqlite3Btree*`); a version bump needs a look at it.
