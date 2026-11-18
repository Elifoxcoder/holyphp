#!/usr/bin/env bash
# deploy-vps.sh — bringt HolyPHP auf den hphp.ch-VPS.
#
#   bash scripts/deploy-vps.sh              # alles (Standard)
#   bash scripts/deploy-vps.sh --no-build   # nur Dateien kopieren, nicht bauen
#   HPHP_VPS_HOST=root@1.2.3.4 bash scripts/deploy-vps.sh
#
# Schritte:
#   1. Quellen            -> /opt/hphp            (Compiler + Registry)
#   2. Build auf dem VPS  -> /opt/hphp/hphp       (Linux-ELF, gcc)
#   3. docs/              -> /var/www/hphp.ch     (Website inkl. Playground)
#   4. tools/playground-api/index.php -> /opt/hphp-playground/api
#   5. hphp-registry neu starten, Live-Check gegen https://hphp.ch
#
# Der VPS steht in scripts/vps.env (HPHP_VPS_HOST); aus manchen Netzen
# braucht es dafür eine VPN-Verbindung. Ohne SSH bricht das Skript ab, statt
# etwas halb deployt zu lassen.
set -euo pipefail
cd "$(dirname "$0")/.."

# shellcheck source=scripts/vps.env
source scripts/vps.env
HOST="$HPHP_VPS_HOST"
SSH_BIN="${HPHP_SSH_BIN:-ssh}"
SCP_BIN="${HPHP_SCP_BIN:-scp}"
REMOTE=/opt/hphp
SITE=/var/www/hphp.ch
API=/opt/hphp-playground/api

BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

step() { printf '\n== %s\n' "$*"; }

# Kein --delete: der VPS hat ein eigenes lib/ (import "ui" & co.) und
# installierte Pakete, die im Repo an anderer Stelle liegen.
step "1/5 Quellen -> $HOST:$REMOTE"
# Die Muster sind mit ./ verankert, damit sie nur im Repo-Wurzelverzeichnis
# greifen — sonst würde das Muster "hphp" auch packages/extension/hphp/ treffen.
# Kopiert wird per tar über ssh: rsync ist nicht auf jedem Rechner installiert.
# bewusst ohne --delete: der VPS hat eigene Dateien (lib/, Pakete, dl/), die
# nicht verschwinden dürfen.
tar czf - \
  --exclude='./.git' \
  --exclude='./build' \
  --exclude='./hphp' \
  --exclude='./hphp.exe' \
  --exclude='./benchmarks' \
  --exclude='./scratch' \
  --exclude='*.o' \
  --exclude='./packaging/windows/out' \
  --exclude='./packaging/windows/stage' \
  . | "$SSH_BIN" "$HOST" "tar xzf - -C $REMOTE"

if [[ $BUILD == 1 ]]; then
  step "2/5 Build auf dem VPS"
  "$SSH_BIN" "$HOST" "cd $REMOTE && bash scripts/build.sh"
  "$SSH_BIN" "$HOST" "$REMOTE/hphp --version"
else
  step "2/5 Build übersprungen (--no-build)"
fi

step "3/5 docs/ -> $SITE"
# Kein --delete: unter der Site liegen auch dl/ (Installer) und weitere
# Verzeichnisse, die nichts mit docs/ zu tun haben.
tar czf - -C docs . | "$SSH_BIN" "$HOST" "tar xzf - -C $SITE"

step "4/5 Playground-API + Live-Runner"
"$SCP_BIN" -q tools/playground-api/index.php "$HOST:$API/index.php"
"$SCP_BIN" -q tools/playground-api/live.py "$HOST:$API/live.py"
"$SCP_BIN" -q tools/playground-api/hphp-playground-container.py "$HOST:/tmp/hphp-playground-container"
"$SCP_BIN" -q tools/playground-api/hphp-playground-jobs.mount "$HOST:/tmp/hphp-playground-jobs.mount"
"$SCP_BIN" -q tools/playground-api/container/Dockerfile "$HOST:/tmp/hphp-playground-Dockerfile"
"$SCP_BIN" -q tools/playground-api/hphp-playground-live.service "$HOST:/etc/systemd/system/hphp-playground-live.service"
"$SCP_BIN" -q tools/playground-api/hphp-playground.service "$HOST:/etc/systemd/system/hphp-playground.service"
"$SCP_BIN" -q tools/playground-api/hphp-playground.slice "$HOST:/etc/systemd/system/hphp-playground.slice"
"$SCP_BIN" -q tools/playground-api/hphp-playground.tmpfiles.conf "$HOST:/etc/tmpfiles.d/hphp-playground.conf"
"$SSH_BIN" "$HOST" "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq docker.io sudo && systemctl enable --now docker && install -o root -g root -m 0755 /tmp/hphp-playground-container /usr/local/sbin/hphp-playground-container && install -o root -g root -m 0644 /tmp/hphp-playground-jobs.mount '/etc/systemd/system/var-lib-hphp\\x2dplayground-jobs.mount' && install -d -o root -g root -m 0755 /opt/hphp-playground/container /var/lib/hphp-playground/jobs && install -o root -g root -m 0644 /tmp/hphp-playground-Dockerfile /opt/hphp-playground/container/Dockerfile && cp $REMOTE/hphp /opt/hphp-playground/container/hphp && docker build --pull -t hphp-playground:latest /opt/hphp-playground/container && printf '%s\\n' 'hphpplay ALL=(root) NOPASSWD: /usr/local/sbin/hphp-playground-container' > /etc/sudoers.d/hphp-playground-container && chmod 0440 /etc/sudoers.d/hphp-playground-container && visudo -cf /etc/sudoers.d/hphp-playground-container && systemctl daemon-reload && systemd-tmpfiles --create /etc/tmpfiles.d/hphp-playground.conf && systemctl enable --now 'var-lib-hphp\\x2dplayground-jobs.mount'"
"$SSH_BIN" "$HOST" "chmod 755 $API/live.py && systemctl daemon-reload \
  && systemd-tmpfiles --create /etc/tmpfiles.d/hphp-playground.conf \
  && systemctl enable --now hphp-playground hphp-playground-live \
  && systemctl restart hphp-playground hphp-playground-live \
  && systemctl is-active hphp-playground hphp-playground-live"

step "5/5 Services + Live-Check"
"$SSH_BIN" "$HOST" "systemctl restart hphp-registry && systemctl is-active hphp-registry hphp-playground"

fail() { echo "LIVE-CHECK FEHLGESCHLAGEN: $*" >&2; exit 1; }

# Farben: println(..., GREEN) muss im Output als ANSI-Sequenz ankommen.
out=$(curl -fsS -X POST $HPHP_SITE/api/run \
  -H 'Content-Type: application/json' \
  --data '{"filename":"main.hphp","code":"println(\"hi\", GREEN);"}')
echo "$out" | grep -q 'u001b\[0;32m' || fail "keine Farbe in der Playground-Ausgabe: $out"

# input(): die Zeile aus dem stdin-Feld muss ankommen.
out=$(curl -fsS -X POST $HPHP_SITE/api/run \
  -H 'Content-Type: application/json' \
  --data '{"filename":"main.hphp","code":"$n = input(\"> \"); println(\"got:\" . $n);","stdin":"Alice\n"}')
echo "$out" | grep -q 'got:Alice' || fail "input() hat die stdin-Zeile nicht gelesen: $out"

# Der Playground selbst muss die neue Seite ausliefern.
page=$(curl -fsS $HPHP_SITE/playground.html)
grep -q 'Live input' <<<"$page" \
  || fail "der neue Playground (Live-Input) ist nicht online"

# Live-Runner muss über nginx erreichbar sein (404/410 sind ok, 502 nicht).
code=$(curl -s -o /dev/null -w '%{http_code}' $HPHP_SITE/api/live/events)
[[ "$code" == "404" || "$code" == "410" ]] \
  || fail "der Live-Runner antwortet nicht ueber nginx (status $code)"

echo
echo "deploy ok: Compiler, Docs, Playground-API, Live-Runner und Live-Checks sind aktuell."
