#!/usr/bin/env bash
# cpu per request for one endpoint, both servers, identical conditions.
#   bash cpureq.sh <path> [script] [conns] [duration]
set -u
ROOT=${CROOT:-/root/challenge}
SEED="$ROOT/seed/feed.db"
LUA="$ROOT/lua"
RUN="$ROOT/run"
HOLY_BIN=${HOLY_BIN:-$ROOT/holy_challenge_server}
RUST_BIN=${RUST_BIN:-$ROOT/submissions/rust-epoll-scooter1337/target/release/twelve-rust-poll}
JWT=twelve-dollar-challenge
PATH_=${1:-/feed}
SCRIPT=${2:-}
CONN=${3:-64}
DUR=${4:-6}

kill_by() { for p in $(pgrep -f "$1"); do kill -9 "$p" 2>/dev/null; done; }
stop_all() { kill_by "holy_challenge_serve[r]"; kill_by "twelve-rust-po[l]"; sleep 0.4; }

wait_up() { for _ in $(seq 1 100); do curl -s -o /dev/null -m 1 "http://127.0.0.1:$1/health" && return 0; sleep 0.1; done; return 1; }

measure() { # label bin port
    local label=$1 bin=$2 port=$3
    echo "[$label] starting $(basename "$bin") on :$port"
    stop_all
    rm -f "$RUN/db.db-wal" "$RUN/db.db-shm"
    cp "$SEED" "$RUN/db.db"
    ( cd "$RUN" && setsid -f env HOST=0.0.0.0 PORT="$port" SQLITE_PATH="$RUN/db.db" JWT_SECRET="$JWT" \
        "$bin" >"$RUN/$label.log" 2>&1 </dev/null )
    wait_up "$port" || { echo "$label: START FAILED"; return; }
    local pid; pid=$(pgrep -f "$(basename "$bin")" | head -1)
    sleep 1
    local u0; u0=$(awk '{print $14+$15}' "/proc/$pid/stat")
    local args=(-t1 -c"$CONN" -d"${DUR}s" --latency)
    [ -n "$SCRIPT" ] && args+=(-s "$SCRIPT")
    local out; out=$(cd "$LUA" && wrk "${args[@]}" "http://127.0.0.1:$port$PATH_" 2>&1)
    echo "$out" > "$RUN/$label.wrk"
    local rps; rps=$(echo "$out" | awk '/Requests\/sec:/ {print $2}')
    if [ -z "$rps" ]; then echo "$label: no wrk result"; sed -n '1,15p' "$RUN/$label.wrk"; stop_all; return; fi
    local lat; lat=$(echo "$out" | awk '/99%/{v=$2} END{gsub(/[a-z]/,"",v); print v}')
    [ -z "$lat" ] && lat=0
    local u1; u1=$(awk '{print $14+$15}' "/proc/$pid/stat")
    stop_all
    local ticks=$((u1 - u0))
    python3 - "$label" "$rps" "$ticks" "$lat" <<'PY'
import os, sys
label, rps, ticks, lat = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
dur = float(os.environ.get('DUR', '6'))
secs = ticks / 20.0                       # USER_HZ = 100
print(f"{label:6} rps={rps:9.0f}  p99={lat:7.2f}ms  cpu={secs:6.3f}s "
      f" cpu/req={secs*1e6/rps:6.1f}us  cpu_share={100*secs/dur:5.1f}%")
PY
}

export DUR=$DUR
echo "=== endpoint $PATH_ script=${SCRIPT:-none} conns=$CONN dur=${DUR}s ==="
measure holy "$HOLY_BIN" 8081
measure rust "$RUST_BIN" 8080
