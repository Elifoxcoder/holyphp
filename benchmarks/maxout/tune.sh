#!/bin/bash
# tune.sh — A/B the HolyPHP engine switches on the VPS.
set -u
cd /root/bench/run
for cfg in "1 1" "0 1" "1 0" "0 0"; do
  set -- $cfg
  MR=$1; DEFER=$2
  pkill -x holy_server; sleep 0.3
  rm -f t.db t.db-wal t.db-shm
  cp ../db/base.db t.db
  setsid -f env PORT=8081 DB_PATH=/root/bench/run/t.db HPHP_MR=$MR HPHP_DEFER=$DEFER \
      /root/bench/holy_server >/tmp/tune.log 2>&1 </dev/null
  for _ in $(seq 1 50); do curl -s -o /dev/null -m 1 http://127.0.0.1:8081/health && break; sleep 0.1; done
  echo "--- MR=$MR DEFER=$DEFER"
  echo -n "health: "; wrk -t1 -c64 -d5s http://127.0.0.1:8081/health 2>/dev/null | awk '/Requests\/sec/ {print $2}'
  curl -s -o /dev/null http://127.0.0.1:8081/feed
  echo -n "feed:   "; wrk -t1 -c64 -d5s http://127.0.0.1:8081/feed 2>/dev/null | awk '/Requests\/sec/ {print $2}'
done
pkill -x holy_server
