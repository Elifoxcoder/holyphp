# Archived C engines (no longer part of the runtime)

`engine_v2.c` (MaxOut contract) and `engine_challenge.c` (challenge spec) were
once embedded in `src/hphpc/runtime/hphp_std.c` and registered as the builtins
`holy_maxout_start()` / `holy_challenge_start()`. That made every Linux program
link `libsqlite3` and `liburing` no matter what it did, so the engines were
moved out. They are kept here for the benchmark results in `RESULTS.md` and
remain runnable; the language runtime no longer contains them and a program
that uses neither module links neither library.

## Build

The engines need SQLite and liburing. They also use the pre-existing
challenge-style `liburing` API directly (`io_uring_queue_init`, provided buffer
rings), so they link `-luring` even though the new optional runtime module
(`src/hphpc/runtime/hphp_uring.c`) talks to the kernel through raw syscalls and
does not.

```sh
# MaxOut engine (default port 8080, DB_PATH or benchmark.db)
gcc -O2 -o holy_server engine_v2.c engine_main.c -lsqlite3 -luring

# challenge-spec engine (default port 80, JWT_SECRET from the environment)
gcc -O2 -DENGINE_CHALLENGE -o holy_challenge_server \
    engine_challenge.c engine_main.c -lsqlite3 -luring

PORT=8081 DB_PATH=/tmp/feed.db ./holy_challenge_server
```

`engine_standalone.h` supplies the few value helpers the engines used from the
HolyPHP runtime (`hval`, `hstr`, `hp_of_int`, `hp_val_to_int`,
`hp_val_to_str`, `hp_throw_str`); `engine_main.c` is the entry point that reads
`PORT` and `DB_PATH`.

The SQLite schema has to exist before the server starts:
`sqlite3 feed.db < ../seed.sql` for MaxOut, `../../.agent/challenge/schema.sql`
or the challenge fixture for the challenge engine.

`bench.sh` and `challenge/bench_challenge.sh` still expect the binary names
`holy_server` / `holy_challenge_server`; build them with the commands above.
