#!/usr/bin/env bash
set -u
pkill -x rust_server 2>/dev/null || true
sleep 0.3
cd /root/bench/run
setsid -f env PORT=8080 DB_PATH=/root/bench/db/rust.db strace -f -tt -e trace=io_uring_setup,io_uring_enter,accept,accept4,recvfrom,recvmsg,sendto,sendmsg,read,write -o /tmp/rust.strace /root/bench/rust_server >/root/bench/results/rust.manual.log 2>&1 </dev/null
sleep 1
pid=$(pidof rust_server)
echo "pid=$pid"
timeout 3 curl -s -o /dev/null -w 'code=%{http_code}\n' http://127.0.0.1:8080/health || true
sleep 0.5
echo "--- strace tail ---"
tail -80 /tmp/rust.strace
echo "--- log ---"
cat /root/bench/results/rust.manual.log
