#!/usr/bin/env bash
set -u
pkill -9 -x rust_server 2>/dev/null || true
sleep 0.5
pidof rust_server || true
install -m 0755 /root/benchmark/rust_server/target/release/rust_server /root/bench/rust_server
ls -l /root/bench/rust_server
cd /root/bench/run
setsid -f env PORT=8080 DB_PATH=/root/bench/db/rust.db /root/bench/rust_server >/root/bench/results/rust.manual.log 2>&1 </dev/null
sleep 0.7
ss -ltnp | grep -E ':(8080|8081)' || true
echo "--- health ---"
timeout 3 curl -s -o /dev/null -w 'code=%{http_code} bytes=%{size_download} time=%{time_total}\n' http://127.0.0.1:8080/health
echo "--- feed ---"
timeout 3 curl -s http://127.0.0.1:8080/feed | head -c 240; echo
echo "--- post ---"
timeout 3 curl -s -X POST -d '{"author":"zz","content":"cc"}' http://127.0.0.1:8080/posts; echo
echo "--- like ---"
timeout 3 curl -s -X POST 'http://127.0.0.1:8080/like?id=1'; echo
echo "--- feed again ---"
timeout 3 curl -s http://127.0.0.1:8080/feed | head -c 240; echo
echo "--- db ---"
sqlite3 /root/bench/db/rust.db 'select id,author,content,likes from posts order by id desc limit 2;'
echo "--- log ---"
cat /root/bench/results/rust.manual.log
