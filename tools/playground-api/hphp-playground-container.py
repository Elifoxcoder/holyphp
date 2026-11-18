#!/usr/bin/python3
"""Narrow root-owned Docker launcher for untrusted HolyPHP programs."""
import os
import pwd
import re
import signal
import subprocess
import sys
import tempfile
import time

DOCKER = "/usr/bin/docker"
IMAGE = "hphp-playground:latest"
ROOT = "/var/lib/hphp-playground/jobs"
MODES = {"api": ("/var/lib/hphp-playground/jobs/api", 29, "8:10"),
         "live": ("/var/lib/hphp-playground/jobs/live", 295, "15:20")}
NAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}\.hphp$")
current = None
container = None


def stop(signum, _frame):
    global current
    if current and current.poll() is None:
        current.terminate()
        try:
            current.wait(timeout=5)
        except subprocess.TimeoutExpired:
            current.kill()
    try:
        remove_container()
    except Exception:
        pass
    raise SystemExit(128 + signum)


def remove_container():
    if container:
        subprocess.run([DOCKER, "rm", "-f", container], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=5)


def main():
    global current, container
    if os.geteuid() != 0 or os.environ.get("SUDO_UID") != str(pwd.getpwnam("hphpplay").pw_uid):
        raise SystemExit("launcher may only be called by hphpplay via sudo")
    if len(sys.argv) != 5:
        raise SystemExit("usage: launcher api|live run|check JOB_DIR FILE.hphp")
    service, action, job, filename = sys.argv[1:]
    if service not in MODES or action not in ("run", "check") or not NAME_RE.fullmatch(filename):
        raise SystemExit("invalid playground request")
    base, wall_limit, cpu_limit = MODES[service]
    if action == "check":
        cpu_limit = "1:2"
    if not os.path.isabs(job) or os.path.realpath(job) != job:
        raise SystemExit("invalid job directory")
    if os.path.dirname(job) != base:
        raise SystemExit("job directory is outside its workspace")
    st = os.stat(job, follow_symlinks=False)
    if not os.path.isdir(job) or st.st_uid != pwd.getpwnam("hphpplay").pw_uid or st.st_mode & 0o777 != 0o700:
        raise SystemExit("invalid job directory ownership or permissions")
    source = os.path.join(job, filename)
    if os.path.islink(source) or not os.path.isfile(source):
        raise SystemExit("invalid source file")
    if os.stat(source, follow_symlinks=False).st_size > 65536:
        raise SystemExit("source exceeds size limit")

    container = "hphp-play-" + next(tempfile._get_candidate_names())
    cmd = [DOCKER, "run", "--rm", "--name", container, "--pull=never",
           "--network=none", "--read-only",
           "--user=%d:%d" % (pwd.getpwnam("hphpplay").pw_uid, pwd.getpwnam("hphpplay").pw_gid),
           "--memory=384m", "--memory-swap=384m", "--cpus=0.75",
           "--cgroup-parent=hphp-playground.slice",
           "--pids-limit=64", "--cap-drop=ALL", "--security-opt=no-new-privileges",
           "--log-driver=none", "--ulimit", "nofile=256:256",
           "--ulimit", "fsize=67108864:67108864", "--ulimit", "core=0:0",
           "--tmpfs", "/tmp:rw,noexec,nosuid,nodev,size=64m,nr_inodes=4096,mode=1777",
           "--mount", "type=bind,src=" + job + ",dst=/workspace",
           "--workdir=/workspace", "--env", "HPHP_RUN_CPU_LIMIT=" + cpu_limit,
           "-i"]
    if os.isatty(0):
        cmd.append("-t")
    cmd.extend([IMAGE, action, "/workspace/" + filename])

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, stop)
    current = subprocess.Popen(cmd)
    deadline = time.monotonic() + wall_limit
    try:
        while current.poll() is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                current.terminate()
                try:
                    current.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    current.kill()
                remove_container()
                return 124
            try:
                return current.wait(timeout=min(0.25, remaining))
            except subprocess.TimeoutExpired:
                pass
        return current.returncode
    finally:
        remove_container()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.TimeoutExpired:
        sys.exit("container cleanup timed out")
