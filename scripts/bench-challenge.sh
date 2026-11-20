#!/usr/bin/env bash
# bench-challenge.sh — load-test the challenge server through the same start
# settings the acceptance suite uses, and print requests/s, latency
# percentiles and RSS for every run.
#
#   bash scripts/bench-challenge.sh --root . --tag new --path /feed
#   bash scripts/bench-challenge.sh --root ../hp-main-bench --tag old --path /feed
#
# The server is built with $ROOT/hphp (build it first, see scripts/build.sh),
# started through benchmarks/challenge/start.sh on a fresh copy of the fixture
# database, warmed up for a few seconds and then measured with `oha`. Needs
# Linux (io_uring) and `oha` (https://github.com/hatoo/oha); both are what the
# challenge measurements used.
#
#   --root DIR          checkout to measure (default: repository root)
#   --tag NAME          label for this series (default: server)
#   --path PATH         request path (default: /feed)
#   --duration TIME     oha -z value (default: 30s)
#   --connections N     oha -c value (default: 256)
#   --runs N            measured runs (default: 3)
#   --warmup TIME       discarded warm-up (default: 5s)
#   --port N            listen port (default: 3000)
#   --out DIR           where the oha JSON files go
#                       (default: /tmp/hphp-bench-<tag>)
#
# Prints one line per run:
#   tag,path,run,requests_per_sec,p50_ms,p95_ms,p99_ms,rss_kb,status
# and a median line at the end.
set -u

SELF=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$SELF
TAG=server
PATH_REQ=/feed
DURATION=30s
CONNECTIONS=256
RUNS=3
WARMUP=5s
PORT=${PORT:-3000}
OUT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --root) ROOT=$2; shift 2 ;;
        --tag) TAG=$2; shift 2 ;;
        --path) PATH_REQ=$2; shift 2 ;;
        --duration) DURATION=$2; shift 2 ;;
        --connections) CONNECTIONS=$2; shift 2 ;;
        --runs) RUNS=$2; shift 2 ;;
        --warmup) WARMUP=$2; shift 2 ;;
        --port) PORT=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "bench-challenge.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

[ -n "$OUT" ] || OUT=/tmp/hphp-bench-$TAG
mkdir -p "$OUT"

HPHP=$ROOT/hphp
if [ ! -x "$HPHP" ] && [ -x "$HPHP.exe" ]; then HPHP=$HPHP.exe; fi
[ -x "$HPHP" ] || { echo "bench-challenge.sh: no compiler at $ROOT/hphp (run scripts/build.sh there)" >&2; exit 1; }
command -v oha >/dev/null 2>&1 || { echo "bench-challenge.sh: 'oha' is required" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "bench-challenge.sh: 'curl' is required" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "bench-challenge.sh: 'python3' is required" >&2; exit 1; }

CHALLENGE=$ROOT/benchmarks/challenge
[ -f "$CHALLENGE/server.hphp" ] || { echo "bench-challenge.sh: no $CHALLENGE/server.hphp" >&2; exit 1; }
[ -f "$CHALLENGE/fixture/feed.db" ] || { echo "bench-challenge.sh: missing fixture/feed.db" >&2; exit 1; }

mkdir -p "$CHALLENGE/bin"
echo "== building $CHALLENGE/bin/server"
"$HPHP" build "$CHALLENGE/server.hphp" -o "$CHALLENGE/bin/server" || exit 1

WORK=$(mktemp -d)
DB=$WORK/feed.db
cp "$CHALLENGE/fixture/feed.db" "$DB"
PID=""
cleanup() {
    if [ -n "$PID" ]; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

export SQLITE_PATH=$DB
export JWT_SECRET=twelve-dollar-challenge
export HOST=127.0.0.1
export PORT
export HPHP_BG_CHECKPOINT=${HPHP_BG_CHECKPOINT:-1}

echo "== starting $TAG on 127.0.0.1:$PORT ($CHALLENGE/start.sh)"
bash "$CHALLENGE/start.sh" > "$OUT/server-$TAG.log" 2>&1 &
PID=$!
up=0
for _ in $(seq 1 120); do
    if curl -sf -m 1 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then up=1; break; fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.5
done
if [ "$up" != 1 ]; then
    echo "bench-challenge.sh: server did not become healthy" >&2
    tail -20 "$OUT/server-$TAG.log" >&2
    exit 1
fi

URL="http://127.0.0.1:$PORT$PATH_REQ"
echo "== warm-up $WARMUP"
oha -z "$WARMUP" -c "$CONNECTIONS" --no-tui --output-format json "$URL" >/dev/null 2>&1 || true

echo "tag,path,run,requests_per_sec,p50_ms,p95_ms,p99_ms,rss_kb,status"
for run in $(seq 1 "$RUNS"); do
    slug=$(echo "$PATH_REQ" | tr -c 'A-Za-z0-9' '_')
    json=$OUT/$TAG-$slug-run$run.json
    oha -z "$DURATION" -c "$CONNECTIONS" --no-tui \
        --output-format json "$URL" > "$json" 2>"$OUT/$TAG-run$run.err" || true
    rss=$(ps -o rss= -p "$PID" 2>/dev/null | tr -d ' ')
    [ -n "$rss" ] || rss=0
    python3 - "$json" "$TAG" "$PATH_REQ" "$run" "$rss" <<'PY'
import json, sys
path, tag, req, run, rss = sys.argv[1:6]
d = json.load(open(path))
s = d["summary"]["requestsPerSec"]
lat = d["metrics"]["latency_ms"]
codes = d.get("statusCodeDistribution", {})
status = ",".join(f"{k}:{v}" for k, v in sorted(codes.items())) or "-"
print(f"{tag},{req},{run},{s:.1f},{lat['p50']:.3f},{lat['p95']:.3f},{lat['p99']:.3f},{rss},{status}")
PY
done

python3 - "$OUT" "$TAG" "$(echo "$PATH_REQ" | tr -c 'A-Za-z0-9' '_')" <<'PY'
import glob, json, os, statistics, sys
out, tag, slug = sys.argv[1], sys.argv[2], sys.argv[3]
rows = []
for path in sorted(glob.glob(os.path.join(out, tag + "-" + slug + "-run*.json"))):
    d = json.load(open(path))
    lat = d["metrics"]["latency_ms"]
    rows.append((d["summary"]["requestsPerSec"], lat["p50"], lat["p95"], lat["p99"]))
if rows:
    print("median: rps=%.1f p50=%.3f p95=%.3f p99=%.3f (%d runs)" % (
        statistics.median(r[0] for r in rows),
        statistics.median(r[1] for r in rows),
        statistics.median(r[2] for r in rows),
        statistics.median(r[3] for r in rows), len(rows)))
PY
