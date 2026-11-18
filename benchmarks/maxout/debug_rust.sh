#!/usr/bin/env bash
set -u
pid=$(pidof rust_server || true)
echo "pid=$pid"
if [ -n "$pid" ]; then
  awk '/State|Threads/' /proc/$pid/status
fi
ss -ltnp | grep -E ':(8080|8081)' || true
echo "--- curl ---"
timeout 3 curl -sv http://127.0.0.1:8080/health 2>&1 | tail -20
echo "--- log ---"
tail -20 /root/bench/results/rust.manual.log
