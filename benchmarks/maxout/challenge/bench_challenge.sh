#!/usr/bin/env bash
# bench_challenge.sh - HolyPHP vs the official Rust submission, same spec,
# same seed, same load generator, interleaved.
#
#   bash bench_challenge.sh [reps] [duration]
#
# Results: /root/challenge/results/summary.rep<N>.csv and one wrk log each.
set -u
HOLY_BIN=${HOLY_BIN:-/root/challenge/holy_challenge_server}
RUST_BIN=${RUST_BIN:-/root/challenge/submissions/rust-epoll-scooter1337/target/release/twelve-rust-poll}
ROOT=${CROOT:-/root/challenge}
SEED="$ROOT/seed/feed.db"
LUA="$ROOT/lua"
RUN="$ROOT/run"
RES="$ROOT/results"
REPS=${1:-3}
DUR=${2:-10}
WARM=3
THREADS=1
CONN=${CONN:-64}
HOLY_PORT=8081
RUST_PORT=8080
JWT=twelve-dollar-challenge

mkdir -p "$RUN" "$RES"
: > "$RES/summary.all.csv"

kill_by() { for p in $(pgrep -f "$1"); do kill -9 "$p" 2>/dev/null; done; }

wait_up() {
    for _ in $(seq 1 100); do
        curl -s -o /dev/null -m 1 "http://127.0.0.1:$1/health" && return 0
        sleep 0.1
    done
    return 1
}

start_server() { # bin port db logfile
    ( cd "$RUN" && setsid -f env HOST=0.0.0.0 PORT="$2" SQLITE_PATH="$3" JWT_SECRET="$JWT" \
        "$1" >"$4" 2>&1 </dev/null )
    wait_up "$2"
}

stop_servers() {
    kill_by "holy_challenge_serve[r]"
    kill_by "twelve-rust-po[l]"
    sleep 0.4
}

fresh_db() { # tag
    rm -f "$RUN/$1.db" "$RUN/$1.db-wal" "$RUN/$1.db-shm"
    cp "$SEED" "$RUN/$1.db"
}

wrk_one() { # url script conns tag
    local url=$1 script=$2 conns=$3 tag=$4
    # run from the script directory: the Lua scripts dofile("tokens.lua")
    local args=(-t"$THREADS" -c"$conns" -d"$WARM"s --latency)
    [ -n "$script" ] && args+=(-s "$script")
    ( cd "$LUA" && wrk "${args[@]}" "$url" >/dev/null 2>&1 )
    args=(-t"$THREADS" -c"$conns" -d"$DUR"s --latency)
    [ -n "$script" ] && args+=(-s "$script")
    ( cd "$LUA" && wrk "${args[@]}" "$url" > "$RES/$tag.wrk" 2>&1 )
    local rps p99 non2xx
    rps=$(awk '/Requests\/sec:/ {print $2}' "$RES/$tag.wrk")
    p99=$(awk '/99%/ {print $2}' "$RES/$tag.wrk" | tail -1)
    non2xx=$(awk '/Non-2xx or 3xx responses:/ {print $5}' "$RES/$tag.wrk")
    [ -z "$non2xx" ] && non2xx=0
    echo "$tag,$rps,$p99,$non2xx" | tee -a "$RES/summary.all.csv"
}

scenario() { # name path script
    local name=$1 path=$2 script=$3
    local sfile=""
    [ -n "$script" ] && sfile="$script"
    for rep in $(seq 1 "$REPS"); do
        # interleaved order: odd reps holy first, even reps rust first
        if [ $((rep % 2)) -eq 1 ]; then order="holy rust"; else order="rust holy"; fi
        for who in $order; do
            stop_servers
            fresh_db "$who-$name-$rep"
            if [ "$who" = holy ]; then
                start_server "$HOLY_BIN" "$HOLY_PORT" "$RUN/$who-$name-$rep.db" "$RES/$who-$name-$rep.server.log" \
                    || { echo "$who-$name-$rep FAILED to start"; continue; }
                wrk_one "http://127.0.0.1:$HOLY_PORT$path" "$sfile" "$CONN" "$who-$name-$rep"
            else
                start_server "$RUST_BIN" "$RUST_PORT" "$RUN/$who-$name-$rep.db" "$RES/$who-$name-$rep.server.log" \
                    || { echo "$who-$name-$rep FAILED to start"; continue; }
                wrk_one "http://127.0.0.1:$RUST_PORT$path" "$sfile" "$CONN" "$who-$name-$rep"
            fi
            stop_servers
            rm -f "$RUN/$who-$name-$rep.db" "$RUN/$who-$name-$rep.db-wal" "$RUN/$who-$name-$rep.db-shm"
        done
    done
}

echo "=============================================================="
echo " twelve-dollar-server-challenge: HolyPHP vs Rust (official)"
echo " wrk -t$THREADS -c$CONN -d${DUR}s, $REPS interleaved reps, median matters"
echo "=============================================================="

scenario health   /health   ""
scenario feed     /feed     ""
scenario post     /         post.lua
scenario like     /         like.lua
scenario create   /         create.lua
scenario mixed    /         mixed.lua

echo "== done =="
grep -E '^[a-z]+-' "$RES/summary.all.csv" | wc -l
