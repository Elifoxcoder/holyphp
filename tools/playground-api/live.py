#!/usr/bin/env python3
"""HolyPHP playground — live runner.

Startet das eingereichte Programm in einem Pseudo-Terminal und streamt dessen
Ausgabe über Server-Sent Events, damit input() beantwortet werden kann, WÄHLED
das Programm läuft (und nicht vorher in ein Eingabefeld getippt).

    POST /api/live/start    {"code": "...", "filename": "main.hphp"} -> {"sid": "..."}
    GET  /api/live/events?sid=...   text/event-stream: out-Chunks + end
    POST /api/live/input    {"sid": "...", "data": "Alice\n"}
    POST /api/live/stop     {"sid": "..."}

Nur an 127.0.0.1 gebunden — nginx ist der einzige Client. Alles läuft als
unprivilegierter Nutzer in der systemd-Unit hphp-playground-live.
"""
import errno
import fcntl
import json
import os
import pty
import queue
import re
import secrets
import select
import shutil
import signal
import struct
import subprocess
import tempfile
import termios
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HOST = "127.0.0.1"
PORT = 8093
MAX_CODE = 65536          # 64 KiB source
MAX_INPUT = 4096          # bytes per input() answer
MAX_SESSIONS = 2          # bound simultaneous compiler jobs on the 1-vCPU VPS
SESSIONS_PER_IP = 1       # sessions one visitor may keep open
RUN_LIMIT = 300           # absolute cap: compile + run must finish in this
IDLE_LIMIT = 90           # no output and no input for this long -> kill
                          # (output and an input() answer both count as
                          # activity, so a visitor may type as long as they
                          # like — the clock only runs while nothing happens)
SESSION_TTL = 120         # keep a finished session this long for late readers
MAX_OUT = 262144          # 256 KiB of retained output
RATE_LIMIT = 5            # starts per IP per minute; nginx adds edge limits
SAFE_NAME = re.compile(r"^[A-Za-z0-9_.-]{1,64}\.hphp$")
SLOT_DIR = "/run/hphp-playground"
JOB_DIR = "/var/lib/hphp-playground/jobs/live"
PLAYGROUND_BLOCKED = {
    "unsafe", "malloc", "free", "memcpy", "memset", "ffi_load", "ffi_call", "heap_dump",
    "shell_exec", "exec", "passthru", "proc_open", "proc_get_status", "proc_start",
    "proc_read", "proc_read_err", "proc_write", "proc_alive", "proc_exit", "proc_close",
    "proc_kill", "system", "popen", "pcntl_exec", "call_user_func",
    "holy_maxout_start", "holy_challenge_start", "thr_spawn", "thr_parallel_map",
    "file_put_contents", "fopen", "fwrite", "fclose", "mkdir", "rmdir", "unlink",
    "rename", "copy", "touch",
}

SESSIONS = {}
LOCK = threading.Lock()


class NoCompileSlot(Exception):
    pass


def acquire_compile_slot():
    """Share two nonblocking compiler slots with the classic API service."""
    for index in range(2):
        path = os.path.join(SLOT_DIR, "compile-slot-%d.lock" % index)
        fd = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return fd
        except BlockingIOError:
            os.close(fd)
    raise NoCompileSlot


def blocked_playground_feature(source):
    """Find restricted identifiers while ignoring comments and quoted strings."""
    code = re.sub(
        r"/\*[\s\S]*?\*/|//[^\r\n]*|#[^\r\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        " ", source,
    )
    if re.search(r"\bwhile\s*\(\s*\(*\s*(?:true|1)\s*\)*\s*\)", code, re.IGNORECASE):
        return "unbounded while loop"
    if re.search(r"\bfor\s*\(\s*;\s*;\s*\)", code, re.IGNORECASE):
        return "unbounded for loop"
    source = code
    i = 0
    n = len(source)
    while i < n:
        if source.startswith("//", i) or source[i] == "#":
            end = source.find("\n", i)
            i = n if end < 0 else end + 1
            continue
        if source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        ch = source[i]
        if ch in "'\"":
            quote = ch
            i += 1
            while i < n:
                if source[i] == "\\":
                    i += 2
                elif source[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            continue
        if ch == "`":
            return "backtick shell syntax"
        if ch.isalpha() or ch == "_":
            end = i + 1
            while end < n and (source[end].isalnum() or source[end] == "_"):
                end += 1
            name = source[i:end].lower()
            if name in PLAYGROUND_BLOCKED:
                return name
            i = end
            continue
        i += 1
    return None


def playground_policy_message(feature):
    return ("Not allowed in the online playground: %s is restricted. "
            "Shell/process calls, raw memory/FFI, file writes and unconditional loops are disabled here." % feature)


class Session:
    def __init__(self, sid, code, name, ip):
        self.sid = sid
        self.ip = ip
        self.slot_fd = None
        self.slot_lock = threading.Lock()
        self.started = time.time()
        self.out = []              # retained output chunks
        self.out_len = 0
        self.subs = []             # queue.Queue of live readers
        self.done = False
        self.exit_code = None
        self.last_activity = time.time()
        self.master = None
        self.proc = None
        self.tmpdir = tempfile.mkdtemp(prefix="hphp_live_", dir=JOB_DIR)
        path = os.path.join(self.tmpdir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(code)
        try:
            self.slot_fd = acquire_compile_slot()
            self._spawn(path)
        except Exception:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            self._release_slot()
            raise

    def _spawn(self, path):
        master, slave = pty.openpty()
        # a sane window size keeps line-based programs happy
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
        self.master = master
        env = os.environ.copy()
        self.proc = subprocess.Popen(
            ["sudo", "-n", "/usr/local/sbin/hphp-playground-container",
             "live", "run", self.tmpdir, os.path.basename(path)], env=env,
            stdin=slave, stdout=slave, stderr=slave,
            cwd=self.tmpdir, start_new_session=True, close_fds=True,
        )
        os.close(slave)
        threading.Thread(target=self._read_loop, daemon=True).start()
        threading.Thread(target=self._watchdog, daemon=True).start()

    # ---- output -------------------------------------------------------
    def _emit(self, text):
        if not text:
            return
        self.out.append(text)
        self.out_len += len(text)
        while len(self.out) > 1 and self.out_len > MAX_OUT:
            self.out_len -= len(self.out.pop(0))
        self.last_activity = time.time()
        for q in list(self.subs):
            try:
                q.put_nowait({"type": "out", "text": text})
            except Exception:
                pass

    def _read_loop(self):
        while True:
            try:
                ready, _, _ = select.select([self.master], [], [], 0.2)
            except (OSError, ValueError):
                break
            if ready:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    break
                if not data:
                    break
                self._emit(data.decode("utf-8", "replace"))
        self._finish()

    def _finish(self):
        if self.done:
            return
        code = None
        if self.proc is not None:
            try:
                code = self.proc.wait(timeout=12)
            except Exception:
                try:
                    self.proc.kill()
                    code = self.proc.wait(timeout=12)
                except Exception:
                    code = -1
        self.exit_code = code
        self.done = True
        self._release_slot()
        self.last_activity = time.time()
        for q in list(self.subs):
            try:
                q.put_nowait({"type": "end", "code": code})
            except Exception:
                pass

    def _watchdog(self):
        while not self.done:
            time.sleep(1.0)
            age_idle = time.time() - self.last_activity
            age_total = time.time() - self.started
            if age_total > RUN_LIMIT or age_idle > IDLE_LIMIT:
                self.kill()
                if age_total > RUN_LIMIT:
                    self._emit("\n[playground] stopped: %ds time limit reached\n" % RUN_LIMIT)
                else:
                    self._emit("\n[playground] stopped: no input or output for %ds\n" % IDLE_LIMIT)
                self._finish()
                return

    # ---- input --------------------------------------------------------
    def write(self, data):
        if self.done or self.master is None:
            return False
        payload = data[:MAX_INPUT].encode("utf-8", "replace")
        try:
            os.write(self.master, payload)
        except OSError as exc:
            if exc.errno in (errno.EIO, errno.EBADF):
                return False
        self.last_activity = time.time()
        return True

    def kill(self):
        if self.proc is None or self.proc.poll() is not None:
            return
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
        except Exception:
            try:
                self.proc.terminate()
            except Exception:
                pass
        try:
            self.proc.wait(timeout=12)
        except Exception:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass

    def cleanup(self):
        self.kill()
        self._release_slot()
        for fd in (self.master,):
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _release_slot(self):
        with self.slot_lock:
            if self.slot_fd is None:
                return
            fd, self.slot_fd = self.slot_fd, None
            try:
                fcntl.flock(fd, fcntl.LOCK_UN)
            finally:
                os.close(fd)


def rate_ok(ip):
    """Fixed window: RATE_LIMIT starts per IP per minute."""
    with LOCK:
        now = int(time.time() // 60)
        key = ip.replace("/", "_")
        bucket = SESSIONS.setdefault("__rate__", {}).setdefault(key, [now, 0])
        if bucket[0] != now:
            bucket[0], bucket[1] = now, 0
        bucket[1] += 1
        return bucket[1] <= RATE_LIMIT


def gc():
    """Drop finished sessions once nothing can read them anymore."""
    now = time.time()
    dead = [s for s in list(SESSIONS.values())
            if isinstance(s, Session) and s.done
            and now - s.last_activity > SESSION_TTL]
    for s in dead:
        SESSIONS.pop(s.sid, None)
        s.cleanup()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):   # quiet: nginx logs the requests
        pass

    def _ip(self):
        return self.headers.get("X-Real-IP", self.client_address[0])

    def _json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        if length <= 0:
            return {}
        raw = self.rfile.read(min(length, 1 << 20))
        try:
            data = json.loads(raw.decode("utf-8", "replace"))
        except Exception:
            return {}
        return data if isinstance(data, dict) else {}

    def _session(self, sid):
        with LOCK:
            return SESSIONS.get(sid)

    # ---------------- GET: event stream ----------------
    def do_GET(self):
        url = urlparse(self.path)
        if url.path != "/api/live/events":
            self._json({"success": False, "error": "Not found"}, 404)
            return
        sid = (parse_qs(url.query).get("sid") or [""])[0]
        sess = self._session(sid)
        if sess is None:
            self._json({"success": False, "error": "unknown session"}, 410)
            return

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache, no-transform")
        self.send_header("X-Accel-Buffering", "no")
        self.send_header("Connection", "close")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        q = queue.Queue()
        with LOCK:
            sess.subs.append(q)
            backlog = list(sess.out)
            finished, code = sess.done, sess.exit_code

        def send(obj):
            try:
                self.wfile.write(("data: " + json.dumps(obj) + "\n\n").encode("utf-8"))
                self.wfile.flush()
                return True
            except Exception:
                return False

        if not send({"type": "start", "sid": sid}):
            with LOCK:
                if q in sess.subs:
                    sess.subs.remove(q)
            return
        for chunk in backlog:
            if not send({"type": "out", "text": chunk}):
                with LOCK:
                    if q in sess.subs:
                        sess.subs.remove(q)
                return
        if finished and not send({"type": "end", "code": code}):
            return

        while True:
            try:
                obj = q.get(timeout=15)
            except queue.Empty:
                if self._session(sid) is None:      # collected meanwhile
                    break
                try:                                  # keep-alive comment
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
                except Exception:
                    break
                continue
            if not send(obj):
                break
            if obj.get("type") == "end":
                break
        with LOCK:
            if q in sess.subs:
                sess.subs.remove(q)

    # ---------------- POST: start / input / stop ----------------
    def do_POST(self):
        url = urlparse(self.path)
        body = self._body()
        ip = self._ip()

        if url.path == "/api/live/start":
            code = body.get("code") if isinstance(body.get("code"), str) else ""
            if not code:
                self._json({"success": False, "error": "No code provided"}, 400)
                return
            if len(code) > MAX_CODE:
                self._json({"success": False, "error": "Code is too long (limit: 64 KiB)"}, 400)
                return
            blocked = blocked_playground_feature(code)
            if blocked:
                self._json({"success": False, "error": playground_policy_message(blocked)}, 403)
                return
            name = body.get("filename") if isinstance(body.get("filename"), str) else "main.hphp"
            if not SAFE_NAME.match(os.path.basename(name)):
                name = "main.hphp"
            if not rate_ok(ip):
                self._json({"success": False, "error": "Too many requests — wait a minute."}, 429)
                return
            gc()
            with LOCK:
                live = [s for s in SESSIONS.values() if isinstance(s, Session) and not s.done]
                mine = [s for s in live if s.ip == ip]
                if len(live) >= MAX_SESSIONS or len(mine) >= SESSIONS_PER_IP:
                    self._json({"success": False, "error": "Too many running programs — wait a moment."}, 429)
                    return
                sid = secrets.token_urlsafe(12)
                try:
                    sess = Session(sid, code, name, ip)
                except NoCompileSlot:
                    self._json({"success": False, "error": "The playground is busy — wait a moment and try again."}, 429)
                    return
                except Exception as exc:
                    self._json({"success": False, "error": "Could not start: %s" % exc}, 500)
                    return
                SESSIONS[sid] = sess
            self._json({"success": True, "sid": sid})
            return

        if url.path == "/api/live/input":
            sid = body.get("sid", "")
            sess = self._session(sid)
            if sess is None:
                self._json({"success": False, "error": "unknown session"}, 410)
                return
            data = body.get("data") if isinstance(body.get("data"), str) else ""
            ok = sess.write(data)
            self._json({"success": ok})
            return

        if url.path == "/api/live/stop":
            sid = body.get("sid", "")
            sess = self._session(sid)
            if sess is not None:
                sess.kill()
                sess._finish()
            self._json({"success": True})
            return

        self._json({"success": False, "error": "Not found"}, 404)


def main():
    srv = ThreadingHTTPServer((HOST, PORT), Handler)
    srv.daemon_threads = True
    print("hphp-playground-live listening on %s:%d" % (HOST, PORT), flush=True)
    try:
        srv.serve_forever()
    finally:
        for s in list(SESSIONS.values()):
            if isinstance(s, Session):
                s.cleanup()


if __name__ == "__main__":
    main()
