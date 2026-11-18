#!/bin/bash
# ab.sh — interleaved A/B on the receive strategy, several repetitions.
set -u
cd /root/bench/run
for rep in 1 2 3; do
  for mr in 1 0; do
    pkill -x holy_server; sleep 0.3
    rm -f t.db t.db-wal t.db-shm
    cp ../db/base.db t.db
    setsid -f env PORT=8081 DB_PATH=/root/bench/run/t.db HPHP_MR=$mr \
        /root/bench/holy_server >/tmp/ab.log 2>&1 </dev/null
    for _ in $(seq 1 50); do curl -s -o /dev/null -m 1 http://127.0.0.1:8081/health && break; sleep 0.1; done
    curl -s -o /dev/null http://127.0.0.1:8081/feed
    h=$(wrk -t1 -c64 -d4s http://127.0.0.1:8081/health 2>/dev/null | awk '/Requests\/sec/ {print $2}')
    f=$(wrk -t1 -c64 -d4s http://127.0.0.1:8081/feed 2>/dev/null | awk '/Requests\/sec/ {print $2}')
    echo "rep=$rep MR=$mr health=$h feed=$f"
  done
done
pkill -x holy_server
