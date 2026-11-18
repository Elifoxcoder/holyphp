#!/usr/bin/env bash
# bench.sh — HolyPHP vs Rust, same box, same DB, same load generator.
#
#   ./bench.sh <holy_server_binary> <rust_server_binary>
#
# Results land in /root/bench/results/summary.csv plus one wrk log per run.
set -u

HOLY_BIN=${1:-/root/bench/holy_server}
RUST_BIN=${2:-/root/benchmark/rust_server/target/release/rust_server}
ROOT=${ROOT:-/root/bench}
DBDIR="$ROOT/db"
RUNDIR="$ROOT/run"
RESDIR="$ROOT/results"
LUA="$ROOT/lua"
DUR=${DUR:-10}
WARM=${WARM:-3}
THREADS=${THREADS:-1}
CONN=${CONN:-64}
HOLY_PORT=${HOLY_PORT:-8081}
RUST_PORT=${RUST_PORT:-8080}

mkdir -p "$DBDIR" "$RUNDIR" "$RESDIR"
: > "$RESDIR/summary.csv"

# ---------------------------------------------------------------- base db
if [ ! -f "$DBDIR/base.db" ]; then
    echo "== seeding 100k posts =="
    sqlite3 "$DBDIR/base.db" < "$ROOT/seed.sql"
    sqlite3 "$DBDIR/base.db" "PRAGMA wal_checkpoint(TRUNCATE);" >/dev/null
fi
sqlite3 "$DBDIR/base.db" "PRAGMA wal_checkpoint(TRUNCATE);" >/dev/null

prep_db() { # $1 = scenario tag
    rm -f "$DBDIR/$1.db" "$DBDIR/$1.db-wal" "$DBDIR/$1.db-shm"
    cp "$DBDIR/base.db" "$DBDIR/$1.db"
}

wait_up() { # $1 = port
    for _ in $(seq 1 100); do
        if curl -s -o /dev/null -m 1 "http://127.0.0.1:$1/health"; then return 0; fi
        sleep 0.1
    done
    return 1
}

start_server() { # $1 = binary  $2 = port  $3 = db  $4 = tag
    local bin=$1 port=$2 db=$3 tag=$4
    ( cd "$RUNDIR" && setsid -f env PORT="$port" DB_PATH="$db" "$bin" \
        >"$RESDIR/$tag.server.log" 2>&1 </dev/null )
    if ! wait_up "$port"; then
        echo "!! $tag did not come up"; tail -20 "$RESDIR/$tag.server.log"; return 1
    fi
    sleep 0.3
    return 0
}

stop_server() { # $1 = binary
    pkill -x "$(basename "$1")" 2>/dev/null
    sleep 0.4
}

wrk_one() { # $1=http-url  $2=script-or-empty  $3=conns  $4=tag
    local url=$1 script=$2 conns=$3 tag=$4
    local args=(-t"$THREADS" -c"$conns" -d"${WARM}s" --latency)
    [ -n "$script" ] && args+=(-s "$script")
    wrk "${args[@]}" "$url" >/dev/null 2>&1
    args=(-t"$THREADS" -c"$conns" -d"${DUR}s" --latency)
    [ -n "$script" ] && args+=(-s "$script")
    wrk "${args[@]}" "$url" > "$RESDIR/$tag.wrk" 2>&1
    local rps lat
    rps=$(awk '/Requests\/sec:/ {print $2}' "$RESDIR/$tag.wrk")
    lat=$(awk '/Latency/ {print $2}' "$RESDIR/$tag.wrk")
    echo "$tag,$rps,$lat" >> "$RESDIR/summary.csv"
    printf "  %-22s %10s req/s   latency %s\n" "$tag" "$rps" "$lat"
}

scenario() { # $1=name  $2=path  $3=script  $4=conns
    local name=$1 path=$2 script=$3 conns=$4
    prep_db "holy-$name"
    if start_server "$HOLY_BIN" "$HOLY_PORT" "$DBDIR/holy-$name.db" "holy-$name"; then
        wrk_one "http://127.0.0.1:$HOLY_PORT$path" "$script" "$conns" "holy-$name"
        stop_server "$HOLY_BIN"
    fi

    prep_db "rust-$name"
    if start_server "$RUST_BIN" "$RUST_PORT" "$DBDIR/rust-$name.db" "rust-$name"; then
        wrk_one "http://127.0.0.1:$RUST_PORT$path" "$script" "$conns" "rust-$name"
        stop_server "$RUST_BIN"
    fi
}

echo "=============================================================="
echo " HolyPHP vs Rust — MaxOut benchmark (1 vCPU VPS)"
echo " load: wrk -t$THREADS -c$CONN -d${DUR}s (client on the same box)"
echo "=============================================================="

scenario health     /health  ""          64
scenario feed       /feed    ""          64
scenario feed_c1    /feed    ""          1
scenario feed_c256  /feed    ""          256
scenario post       /posts   "$LUA/post.lua"   64
scenario like       /like    "$LUA/like.lua"   64
scenario mixed      /feed    "$LUA/mixed.lua"  64

echo
echo "== summary.csv =="
cat "$RESDIR/summary.csv"
