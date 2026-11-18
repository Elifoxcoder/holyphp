#!/usr/bin/env bash
# vsix.sh — die VS-Code-Extension neu erzeugen und installieren.
#
#   bash scripts/vsix.sh                 # genext -> vsce -> code --install-extension
#   bash scripts/vsix.sh --no-install    # nur bauen
#
# Schritt 1: node scripts/genext.js
#            (syncrt data/builtins.json mit src/hphpc/builtins.c — breitert ab,
#             wenn einem Builtin in scripts/builtin-docs.js die Beschreibung fehlt)
# Schritt 2: npx @vscode/vsce package -> packages/extension/hphp/holyphp-<v>.vsix
# Schritt 3: code --install-extension --force (aktualisiert die installierte)
#
# Ohne `code` im PATH wird die VSIX trotzdem gebaut; das Skript sagt dann,
# dass das Installieren von Hand fehlt (und ist damit KEIN grüner Haken).
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=1
[[ "${1:-}" == "--no-install" ]] && INSTALL=0

echo "== genext (builtins aus src/hphpc/builtins.c)"
node scripts/genext.js

echo "== Extension-Tests (node --test)"
( cd packages/extension/hphp && node --test test/*.test.js )

VSIX_DIR=packages/extension/hphp
VERSION=$(node -p "require('./$VSIX_DIR/package.json').version")
VSIX="$VSIX_DIR/holyphp-$VERSION.vsix"

echo "== vsce package -> $VSIX"
( cd "$VSIX_DIR" && npx --yes @vscode/vsce package \
    --allow-missing-repository -o "holyphp-$VERSION.vsix" )

if [[ $INSTALL == 0 ]]; then
  echo "== Installieren übersprungen (--no-install): $VSIX"
  exit 0
fi

if ! command -v code >/dev/null 2>&1; then
  echo "FEHLER: 'code' ist nicht im PATH — $VSIX ist gebaut, aber NICHT installiert." >&2
  echo "        bitte einmalig installieren mit:" >&2
  echo "        code --install-extension $VSIX --force" >&2
  exit 1
fi

echo "== code --install-extension --force"
code --install-extension "$VSIX" --force
code --list-extensions --show-versions | grep -i '^holyphp@' || true
echo "VSIX aktuell: $VSIX"
