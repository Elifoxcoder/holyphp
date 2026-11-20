#!/usr/bin/env bash
# The twelve-dollar challenge against this repository's compiler.
#
#   bash benchmarks/challenge/run.sh              # direct (io_uring) + classic
#   bash benchmarks/challenge/run.sh direct       # only HPHP_DIRECT=1
#   bash benchmarks/challenge/run.sh classic      # only HPHP_DIRECT=0
#
# Builds benchmarks/challenge/server.hphp with ./hphp (building the compiler
# first if it is missing), starts it on a fresh copy of the committed fixture
# database and runs the challenge's 42 checks (test/test.sh). The fixture keeps
# only the rows those checks read, so the suite needs neither the 250 MB seed
# nor a database generator.
#
# Needs: bash, curl, jq, openssl. Direct mode needs Linux (io_uring); on other
# hosts the runtime falls back to the classic real-fd path by itself.
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$PWD

MODE=${1:-both}
case "$MODE" in
    both|direct|classic) ;;
    *) echo "usage: bash benchmarks/challenge/run.sh [both|direct|classic]"; exit 2 ;;
esac

for tool in curl jq openssl; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "run.sh: '$tool' is required (curl, jq and openssl run the challenge suite)"
        exit 1
    }
done

HPHP=${HPHP:-$ROOT/hphp}
if [ ! -x "$HPHP" ] && [ -x "$HPHP.exe" ]; then HPHP=$HPHP.exe; fi
if [ ! -x "$HPHP" ]; then
    echo "== building the compiler"
    bash scripts/build.sh || exit 1
    HPHP=$ROOT/hphp
    [ -x "$HPHP" ] || HPHP=$ROOT/hphp.exe
fi
[ -x "$HPHP" ] || { echo "run.sh: no compiler at $HPHP"; exit 1; }

DIR=benchmarks/challenge
[ -f "$DIR/fixture/feed.db" ] || { echo "run.sh: missing $DIR/fixture/feed.db"; exit 1; }
mkdir -p "$DIR/bin"
echo "== building $DIR/bin/server"
"$HPHP" build "$DIR/server.hphp" -o "$DIR/bin/server" || exit 1

WORK=$(mktemp -d)
PID=""
cleanup() {
    if [ -n "$PID" ]; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

PORT=${PORT:-3000}
export JWT_SECRET=twelve-dollar-challenge
export HOST=127.0.0.1
export PORT
export SQLITE_PATH="$WORK/feed.db"
export HPHP_BG_CHECKPOINT="${HPHP_BG_CHECKPOINT:-1}"
export HPHP_DIRECT

# run_mode <direct 0|1> -> sets MODE_RC
run_mode() {
    local direct=$1 name
    [ "$direct" = 1 ] && name=direct || name=classic
    echo "===== $name (HPHP_DIRECT=$direct)"
    cp "$DIR/fixture/feed.db" "$SQLITE_PATH"
    rm -f "$SQLITE_PATH-wal" "$SQLITE_PATH-shm"
    HPHP_DIRECT="$direct" bash "$DIR/start.sh" > "$WORK/server-$name.log" 2>&1 &
    PID=$!
    local up=0 i
    for i in $(seq 1 60); do
        if curl -sf "http://$HOST:$PORT/health" >/dev/null 2>&1; then up=1; break; fi
        if ! kill -0 "$PID" 2>/dev/null; then break; fi
        sleep 1
    done
    if [ "$up" != 1 ]; then
        echo "FAIL $name: no healthy /health after 60 s"
        tail -30 "$WORK/server-$name.log" | sed 's/^/    /'
        MODE_RC=1
    else
        JWT_SECRET=twelve-dollar-challenge bash "$DIR/test/test.sh" "http://$HOST:$PORT"
        MODE_RC=$?
    fi
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    PID=""
    rm -f "$SQLITE_PATH" "$SQLITE_PATH-wal" "$SQLITE_PATH-shm"
}

rc=0
if [ "$MODE" = both ] || [ "$MODE" = direct ]; then
    MODE_RC=0
    run_mode 1
    [ "$MODE_RC" = 0 ] || rc=1
fi
if [ "$MODE" = both ] || [ "$MODE" = classic ]; then
    MODE_RC=0
    run_mode 0
    [ "$MODE_RC" = 0 ] || rc=1
fi

if [ "$rc" = 0 ]; then
    echo "challenge ok ($MODE)"
else
    echo "challenge FAILED ($MODE)"
fi
exit "$rc"
