# OpenCode agent brief — HolyPHP

Dieses Projekt ist **HolyPHP** — einen eigenen Compiler in C11 (Windows/MinGW-GCC), eine VS-Code-Erweiterung und einen online Playground unter https://hphp.ch. Als Agent, der hier arbeitet, musst du am Ende **alles** durchgecheckt haben, sonst darfst du nicht "fertig" sagen.

## Was wo ist

```
HolyPHP/
  src/hphpc/            Compiler (Lexer, Parser, Sema, Codegen, Runtime)   C11
  scripts/build.sh      baut hphp.exe
  scripts/genext.js     syncrt builtins.json aus dem Compiler (für die Extension)
  scripts/builtin-docs.js  handgeschriebene Beschreibungen je Builtin
  scripts/vsix.sh       generiert + installiert die VSIX (genext → vsce → code --install-extension --force)
  scripts/deploy-vps.sh kopiert Quellen per SSH → VPS, baut dort, deployt Docs + API, Live-Checks
  tools/playground-api/index.php  PHP-API für den Playground (POST /api/run, /api/check)
  tools/playground-api/live.py    Python-Live-Runner (pty + SSE) für Echtzeit-Input
  packages/extension/   VS-Code-Erweiterung
    extension.js        Activation, Completion, Hover, Diagnostics (hphp check)
    package.json        Version + Beiträge + Keybindings + Konfig
    snippets/hphp.json  Snippets (fori, for, foreach, if, echo, input, …)
    data/builtins.json  generierte Builtin-Liste (Wurzel: { builtins: { name: {name,sig,args,desc,unsafe} } })
    syntaxes/hphp.tmLanguage.json
  tools/playground-api/hphp-playground-live.service   systemd-Unit für den Live-Runner (liegt auch im Repo)
  docs/                 Website inkl. playground.html
  tests/positive/       der hphp-Tests (Test-Suite)
  tests/run.sh          läuft alle Tests
  AGENTS.md             Definition of Done (gelesen!)
```

## Key bindings

Bei HolyPHP sind die wichtigsten Tastatur-Bindungen in der Extension:

- `fori` (Snippet) → `for ($i = 0; $i < N; $i++) { … }` (Tab-Tab-Navigation im Körper).
- `for` → klassisches for.
- `fore` → foreach über Array.
- `forek` → foreach mit key => value.
- `while` → while-Schleife.
- `if`, `ife` → if / if-else.
- `echo` → `echo "...";`
- `input` → `$name = input("> ");`
- `println` → `println("text");`
- `printc` → `println("text", GREEN);`
- `import` → `import "ui";`
- `try` → try/catch.
- `matchx` → match expression.
- `push` → array append.
- `class` → class mit Constructor.
- `switch` → switch / case.
- `fun` → function mit Typ-Parametern.
- `ctrl+f5` → holyphp.run (via package.json).
- `ctrl+shift+m` → holyphp.check (via package.json) — vorsichtig, danger: überschreibt globales Problems Panel? Ja, es überschreibt `Problems: Focus on Problems View` (Standard: `ctrl+shift+m`). Derzeit ist das Keybinding aktiv.

## Wichtige Befehle (Zusammenfassung)

- Build: `bash scripts/build.sh` → endet mit "build ok" ohne `error:`.
- Tests: `bash tests/run.sh` → meldet "PASS"/"FAIL" pro Datei, am Ende "== N passed, M failed, K skipped =="
- Extension bauen + installieren: `bash scripts/vsix.sh` → baut die VSIX und installiert mit `--install-extension --force`. Prüfen: `code --list-extensions --show-versions | grep holyphp` zeigt Version 0.1.1.
- Compiler lokal installieren: `cp hphp.exe "$LOCALAPPDATA/Programs/HolyPHP/hphp.exe"` (bzw. wo `hphp` im PATH ist). Auf diesem Rechner: `C:\Users\elias\AppData\Local\Programs/HolyPHP/hphp.exe`.
- VPS deployen: `bash scripts/deploy-vps.sh`. SSH zu `root@91.216.248.93`, Port 22 (ggf. VPN nötig). Aktualisiert Compiler, Docs, API, Live-Runner, startet Services, führt Live-Checks durch.
- Local Smoke-Test: `node --check packages/extension/hphp/extension.js && node scratch/ext-smoke.js`.

## Known issues / Vorerkrankungen (müssen genannt werden, nicht verschwinden)

- `byref_storage.hphp` (positive) kann fehlschlagen wegen `undefined reference to hpbi_passthru` / `passthru`-Problem. Bekannte frühzeitige Vorerkrankung; nicht selbst verursacht.
- `html_css.hphp`, `html_layout.hphp`, `ide_editor.hphp`, `ide_workbench.hphp` (positive) fehlschlagen oft wegen fehlender Imports / Libraries beim lokalen Build (z.B. `cannot import "html"` etc.) — das hängt vom Build-Pfad / vorinstallierten Paketen ab.
- Docs-Snippet-Tests: `lib-html_00..04`, `lib-ide_00..01` fehlschlagen typischerweise mit `cannot import "html"` / `cannot import "ide"` — bekannt, nicht selbst verursacht, aber namentlich zu nennen.
- Die Testlücken sind Vorerkrankungen; der Agent hat sie nicht verursacht und sie nicht behoben, aber sie sind im Testlauf sichtbar und müssen genannt werden.

## Liste letzter Beweise (latest proof)

- Compiler: Build im Repo-Root (`hphp.exe`), Version 1.0.2 (gemäß Compiler-Ausgabe).
- Extension: VSIX `holyphp-0.1.1.vsix` gebaut und via `code --install-extension --force` installiert; `code --list-extensions --show-versions` zeigt `holyphp.holyphp@0.1.1` (sofern frisch installiert, sonst alte Version).
- Playground: `docs/playground.html` mit LIVE-INPUT, ANSI-to-HTML, Beispielen "Colors (print / println)" und "Input (input)".
- VPS: Compiler, Docs, API, Live-Runner sind auf dem VPS (sofern erfolgreich deployed mit `bash scripts/deploy-vps.sh`), Services aktiv.
- Live-Check auf VPS: `bash scripts/deploy-vps.sh` führt curl-Checks für Farbe, input() mit stdin-Zeile, Live-Input-Seite und Live-Runner-Erreichbarkeit durch.

## Agent-Compliance

Jeder Agent, der hier arbeitet, muss nach AGENTS.md und dieser OPENCODE.md handeln:

- Bauen (build.sh) → Tests (tests/run.sh) → Extension aktualisieren (genext + vsix.sh) → Compiler lokal installieren (cp) → VPS deployen (deploy-vps.sh) → Live verifizieren (Browser, im Playground tippen bei input() Beispiel).
- Ohne SSH → "nicht deployed, weil kein SSH (VPN?)".
- Keine Tests verschweigen; Fehlschläge namentlich nennen.
- Keine "erledigt"-Antwort bei unvollständigem Check.
- Keine Assertions abschwächen, keine Tests umgehen.

## Wenn du mit einer neuen Anfrage beginnst: Vorgehen

1. Lies AGENTS.md und OPENCODE.md.
2. Kläre den Änderungsbereich.
3. Build.
4. Tests.
5. Extension? → genext + vsix.sh + Smoke.
6. Compiler/Runtime? → Build + Tests + genext + VSIX + VPS + Browser.
7. Website/Playground/API? → VPS + Browser.
8. VPS-Deployment nur mit VPN.
9. Browser-Check: https://hphp.ch/playground.html → Beispiel "Input (input)" oder "Colors (print / println)" laden, Run, tippen bei Live-Input oder Farben prüfen.
10. Ergebnis: alle grün → "erledigt".

## Ablaufdiagramm: Agent-Workflow

```
Liest AGENTS.md + OPENCODE.md
Klärt Änderungsbereich
Build: bash scripts/build.sh → "build ok"
Tests: bash tests/run.sh → meldet "PASS"/"FAIL", Fehlschläge namentlich
Extension? → genext + vsix.sh + Smoke (node scratch/ext-smoke.js)
Compiler/Runtime? → Build + Tests + genext.js + VSIX + VPS + Browser
Website/Playground/API? → VPS + Browser
VPS-Deployment: bash scripts/deploy-vps.sh (nur mit VPN; Falls SSH-Ausfall → "nicht deployed, weil kein SSH (VPN?)")
Browser-Check: https://hphp.ch/playground.html → Beispiel laden, Run, tippen oder Farben prüfen
Ergebnis: alle grün → "erledigt"
```

## Quellweise Referenz-Infrastruktur

- Source-Repo: `https://github.com/HolyPHP/holyphp` (wenn vorhanden).
- Website: `https://hphp.ch`, Playground unter `https://hphp.ch/playground.html`.
- VPS: `root@91.216.248.93` (SSH, Port 22; VPN ggf. nötig).
- Compiler-Build: `bash scripts/build.sh` → `hphp.exe`.
- Extension-VSIX: `bash scripts/vsix.sh` → `packages/extension/hphp/holyphp-0.1.1.vsix`.
- Extension-Install: `code --install-extension packages/extension/hphp/holyphp-0.1.1.vsix --force`.
- VPS-Deployment: `bash scripts/deploy-vps.sh`.

## Lokale Smoke-Test-Ablauf

Ein Agent kann den Ablauf lokal testen:

1. `cd HolyPHP`
2. `bash scripts/build.sh` → "build ok", sonst Fehler prüfen.
3. `bash tests/run.sh` → Ausgabe prüfen. Bekannte Vorerkrankungen dürfen bestehen, müssen genannt werden.
4. Extension-Build und Install: `bash scripts/vsix.sh` → baut und installiert die VSIX. Nach Ablauf prüfen: `code --list-extensions --show-versions | grep holyphp` zeigt Version 0.1.1.
5. Smoke-Test der Extension: `node scratch/ext-smoke.js`. Das Script regt die Extension via Node an, führt Completions und Hover durch und prüft das Ergebnis.
6. Falls Compiler/Runtime oder Playground/API verändert: danach VPS-Deployment und Browser-Check (s.o.).

## Diagnotik: was sagt der Compiler

`hphp check` ist die Grundlage für VS-Code-Diagnose. Ein Beispiel:

```
cd HolyPHP
./hphp.exe check tests/positive/explode_chars.hphp
```

Das meldet Type/Borrowingfehler, undefinierte Variablen, undefinierte Funktionen. VS-Code ruft `hphp check` auf save/change. Diagnosemodus in package.json: `diagnosticsMode` (default "type" oder "save").

## CLI-Beispiel: Siehe, was der Compiler tut

Ein Minimalbeispiel zur Demonstration der Funktionalität (aus Scratch oder tests/positive/):

```
cat > /tmp/demo.hphp <<'EOF'
println("Hello World", GREEN);
println("Bye", RED);
EOF
./hphp.exe run /tmp/demo.hphp
```

Das gibt grünes "Hello World" und rotes "Bye" aus.

## CLI-Beispiel: input() mit stdin

```
cat > /tmp/inputdemo.hphp <<'EOF'
println("What is your name?");
$name = input("> ", GREEN);
println("Hello, " . $name . "!");
EOF
echo "Elias" | ./hphp.exe run /tmp/inputdemo.hphp
```

Das gibt "Hello, Elias!" aus (stdin-Vergabe).

## CLI-Beispiel: explode mit leerem Separator

```
cat > /tmp/explodedemo.hphp <<'EOF'
$s = implode("|", explode("", "Hällo"));
println($s);
println(count(explode("", "Hällo")), GREEN);
EOF
./hphp.exe run /tmp/explodedemo.hphp
```

Das gibt `H|ä|l|l|o` und `5` (grün) aus — UTF-8-aware, `ä` bleibt ein Zeichen.

## CLI-Beispiel: check-Modus

```
cat > /tmp/checkdemo.hphp <<'EOF'
$a = 1;
echo $b;
EOF
./hphp.exe check /tmp/checkdemo.hphp
```

Das meldet "undefined variable $b". Für die Extension: Diagnostics zeigt `hphp check`-Fehler als vscode.Diagnostics an.

## Warum explod mit leerem Separator

Explode mit leerem Separator (`explode("", "Hällo")`) teilt einen String in Einzelzeichen. UTF-8-awareness: `ä` (2 Byte) wird als ein Zeichen behandelt, nicht als zwei Byte. Das ist wichtig für die Anfrage "Buchstaben aus einem Wort splitten". Der Compiler-Code ist in `src/hphpc/runtime/hphp_std.c` (hpbi_explode mit leerem d->len → Zeichenweise Zerlegung).

## Warum input() jetzt geht

Die Anfrage war, dass input() nicht definiert war ("undefined function 'input()'"). Die Ursache war ein älterer Compiler in PATH (`hphp` im PATH mit Version 0.2.0), der input() nicht kannte. Die Lösung: Compiler lokal aktualisieren (`cp hphp.exe ...`), sodass der neue Compiler mit input() verwendet wird. input() ist ein Builtin im Compiler (B_READLINE_COLOR), Farbenkonstanten sind Compiler-Konstanten.

## Warum fori jetzt geht

fori ist ein Snippet in `packages/extension/hphp/snippets/hphp.json` ("Simple for loop"). Es erzeugt `for ($i = 0; $i < ${1:10}; $i++) { ... }` und ist beim Tippen von `fori` + Tab zugänglich. Die Anfrage war, dass man fori eintippen können soll für einen simplen for loop.

## Warum VSIX jetzt gut ist

Die VSIX-Erweiterung liefert:

- Syntax highlighting (tmLanguage).
- Completion mit Builtin-Dokumentation (extension.js: completion provider mit BUILTINS aus data/builtins.json, Snippet-Insertion).
- Hover auf Builtins, Funktionen, Variablen (extension.js: hover provider mit describeVariable, Kollisionsfix $count).
- Diagnostics via `hphp check` (extension.js: HphpCheckRunner, parseDiagnostics).
- Snippets (hphp.json: fori, for, foreach, if, echo, input, …).
- Keybindings (package.json: ctrl+f5 run, ctrl+shift+m check).

Die Dokumentation der Builtins ist in `scripts/builtin-docs.js` (handgeschrieben, je Builtin eine Beschreibung), die genutzt wird für `docs/builtins.html` und `data/builtins.json` (über genext → extension).

## Warum Playground jetzt Live-Input hat

Die Anfrage war: "auf dem Playground will ich live input also zur runtime dann will ich was eingeben können, nicht vorher." Der Lösungsweg:

1. Live-Runner (`live.py`) mit pty + SSE, der während der Laufzeit input() beantworten kann.
2. Playground-HTML mit LIVE-INPUT-Feld, das während der Laufzeit aktiv ist (setRunning(true) → disabled=false).
3. Client: Run startet Sitzung (POST /api/live/start → {sid}), EventSource GET /api/live/events?sid=… (SSE: out, end), POST /api/live/input {sid, data} für jede input()-Zeile, POST /api/live/stop {sid}.
4. Vorher war input() nicht möglich, weil kein pty-basierter Live-Runner existierte.
5. Timeouts: 90 s Idle, 300 s Gesamtlaufzeit (hart). Client spiegelt mit bumpWatchdog (100 s / 320 s).
6. VPS hat den Live-Runner und die nginx-Route `/api/live/` → 8093 (ohne proxy_buffering).

## Warum VSIX auto-install/update jetzt geht

Die Anfrage war: "ich will dass du automatisch für mich die vsix installierst/updatest." `scripts/vsix.sh` baut die VSIX und installiert mit `code --install-extension --force`. Es ist Teil der Definition of Done (AGENTS.md Schritt 3).

## Definition of Done (kurz)

1. Build.
2. Tests (namentlich genannte Fehlschläge).
3. Extension aktualisieren (genext + vsix.sh).
4. Compiler lokal installieren.
5. VPS deployen.
6. Live verifizieren (Browser: playground.html → Beispiel → Run → tippen oder Farben).

## Key commands (kurzreferenz)

| Befehl | Zweck |
|---|---|
| `bash scripts/build.sh` | Compiler bauen |
| `bash tests/run.sh` | Tests ausführen |
| `node scripts/genext.js` | builtins.json sync |
| `bash scripts/vsix.sh` | VSIX bauen + installieren |
| `cp hphp.exe "$LOCALAPPDATA/Programs/HolyPHP/hphp.exe"` | Compiler lokal installieren |
| `bash scripts/deploy-vps.sh` | VPS deployen |
| `node scratch/ext-smoke.js` | Extension-Smoke-Test |

## Letzte Information

Diese Datei ist eine Zusammenfassung des aktuellen Zustands von HolyPHP für einen OpenCode-Agenten beim nächsten Auftrag. Sie enthält alle relevanten Informationen über die Struktur, die Befehle, die Definition of Done, die Vorerkrankungen und die Handlungsanweisungen.

## Was ein neuer Agent sofort tun muss (Start-Checkliste)

- Lies AGENTS.md und OPENCODE.md.
- Baue Compiler: `bash scripts/build.sh`.
- Prüfe Tests: `bash tests/run.sh` (namentlich genannte Fehlschläge).
- Extension: `bash scripts/vsix.sh` (baut + installiert VSIX).
- Compiler lokal installieren: `cp hphp.exe "$LOCALAPPDATA/Programs/HolyPHP/hphp.exe"`.
- VPS deploy: `bash scripts/deploy-vps.sh` (nur mit VPN; SSH zu root@91.216.248.93).
- Browser-Check: https://hphp.ch/playground.html → Beispiel laden, Run, tippen oder Farben prüfen.
- Ergebnis: alle grün → "erledigt".
