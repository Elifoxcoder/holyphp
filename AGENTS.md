# AGENTS.md — HolyPHP

Pflichtlektüre für jeden, der hier Code ändert, auch für Agenten (Codex,
Claude Code, OpenCode, ChatGPT). Das ist die **einzige** Regelquelle:
`CLAUDE.md` und `OPENCODE.md` verweisen nur hierher.

**Kurzfassung:** Vorher lesen, was es gibt. Während der Arbeit Tests, Doku
und Changelog mitschreiben. Fertig ist eine Aufgabe erst, wenn
`bash scripts/ship.sh --version <x.y.z>` mit `ship ok` endet. Bis dahin heißt
die Antwort „nicht fertig, weil …“, nie „erledigt“.

## 1. Bevor du etwas änderst

1. `bash scripts/check-repo.sh`: Was ist schon kaputt oder veraltet? Nenne
   vorhandene Probleme in deiner Antwort, statt sie stillschweigend zu erben.
2. **Builtins nachschlagen, bevor du welche baust oder benutzt:**
   `docs/builtins.html` (erzeugt aus `src/hphpc/builtins.c` und
   `scripts/builtin-docs.js`). Es gibt über 400. Baue nichts nach, was es
   schon gibt, und erfinde keine Namen.
3. `STATUS.md` und `TODO.md` lesen: Woran wird gerade gearbeitet, was ist
   bekannt kaputt?
4. Auf einem eigenen Branch arbeiten (`git checkout -b <thema>`), nicht direkt
   auf `main`.

## 2. Während der Arbeit

- **Neues Builtin** braucht in derselben Änderung:
  - Eintrag in `src/hphpc/builtins.c`
  - Beschreibung in `scripts/builtin-docs.js`
  - Beispiel mit erwarteter Ausgabe in `scripts/builtin-examples.js` (wird
    ausgeführt und verglichen)
  - einen Test in `tests/positive/`
  Fehlt eins davon, scheitert `check-repo.sh`.
- **Builtins bleiben PHP-artig:** global aufrufbar, ohne Import, mit
  PHP-Namen und PHP-Semantik, wo es ein PHP-Vorbild gibt. Bestehende Builtins
  nicht in ihrer Bedeutung ändern (Programme verlassen sich darauf). Neues
  Verhalten bekommt eine neue Funktion oder einen optionalen Parameter.
- **Spezialbereiche gehören nicht in den Sprachkern** (SQLite, io_uring,
  UI, WebView, Threads): Sie leben als eigenes Runtime-Modul
  (`runtime/hphp_<modul>.c`), das der Compiler nur dazulinkt, wenn ein
  Programm es benutzt (wie heute `hp_thr`, `hp_ui`, `hp_wv2`). Für den Nutzer
  bleiben sie normale Builtins.
- **Bug gefunden?** Erst einen Test, der ihn zeigt (`tests/positive/` bzw.
  `tests/negative/`), dann der Fix.
- **Tests aufräumen:** Wegwerf-Skripte gehören nach `scratch/`, nicht nach
  `tests/` oder ins Repo-Wurzelverzeichnis. `tests/known-failures.txt`
  enthält nur Tests, die wirklich (noch) scheitern. Behobene raus.
- **Changelog:** Jede für Nutzer sichtbare Änderung bekommt eine Zeile unter
  `## Unreleased` in `CHANGELOG.md`, geschrieben für Nutzer, nicht als
  Commit-Log.
- **Website und Doku:** Wer Sprachverhalten, CLI oder Bibliotheken ändert,
  passt die zugehörige Seite in `docs/` an (`lang.html`, `cli.html`,
  `guide/`). Generierte Seiten (`docs/builtin/`, `builtins.html`,
  `docs.html`, `docs/libraries/`) nicht von Hand editieren: `ship.sh`
  erzeugt sie neu (`scripts/gendocs.js`, `scripts/gensite.js`).
- **Bibliotheken** (`packages/lib/<name>/`) haben je eine Seite auf
  `hphp.ch/libraries/<name>/`. Quelle: `docs/libraries/src/<name>.html` mit
  den Abschnitten `overview`, `documentation`, `examples` (der Tab
  „Installation“ wird erzeugt) plus ein Eintrag in `LIBS` in
  `scripts/gensite.js` (Kategorie, Kurzbeschreibung, Stichworte, Rang).
  Ohne beides bricht `gensite.js` ab. Vorschau: `node scripts/gensite.js`.
- Kleine, einzeln verständliche Commits.

## 3. Fertig = `ship.sh ok`

Branch nach `main` mergen, dann:

```bash
bash scripts/ship.sh --version 1.0.3            # alles
bash scripts/ship.sh --version 1.0.3 --local    # ohne Push, Release und Deploy
```

`ship.sh` erledigt, was früher vergessen wurde, und bricht beim ersten
Fehler ab. In dieser Reihenfolge:

1. Vorabprüfung: auf `main`, nichts uncommittet, aktuell mit GitHub, `gh` und
   SSH erreichbar.
2. Changelog: `## Unreleased` wird zu `## <version> — <datum>`.
3. Version überall setzen (`scripts/set-version.sh`): Compiler, Runtime,
   Windows-Installer, `.deb`, AppImage.
4. `scripts/check-repo.sh`.
5. Build (`build ok`, kein `error:`, `hphp.exe --version` stimmt).
6. Tests: keine neuen Fehlschläge, keine veralteten Einträge in
   `tests/known-failures.txt`.
7. Docs neu erzeugen und jedes Builtin-Beispiel ausführen.
8. VS-Code-Extension bauen und installieren.
9. Compiler im PATH ersetzen (`$LOCALAPPDATA/Programs/HolyPHP`).
10. Windows-Installer, Upload auf den VPS, winget-Manifest.
11. `.deb` und AppImage (über WSL).
12. Commit der erzeugten Dateien und Tag `v<version>`.
13. Push nach GitHub (`main` und Tag).
14. GitHub-Release mit allen Installern, Text aus dem Changelog. Die
    Download-Links auf hphp.ch zeigen dorthin.
15. Deploy auf den VPS (`scripts/deploy-vps.sh`, inkl. Live-Checks).
16. Verifikation: VPS-Compiler, GitHub-Release und Website zeigen die neue
    Version.

Danach noch von Hand im Browser: `https://hphp.ch/playground.html`, Beispiel
„Input (input)“ laden, **Run**, in der **LIVE-INPUT**-Box während der Laufzeit
tippen: „Hello, <Name>!“ muss erscheinen. Ein Farb-Beispiel
(`println(…, GREEN)`) muss farbig ankommen.

Scheitert ein Schritt, ist die Antwort: „nicht fertig: Schritt N, Grund“. Ist
der VPS nicht erreichbar (VPN?), gilt dasselbe: „nicht deployt, weil …“.

**Versionsnummern:** Patch (`1.0.x`) für Fixes und neue Builtins, Minor
(`1.x.0`) für neue Sprachfeatures oder Bibliotheken, Major nur bei Brüchen.
Die VS-Code-Extension (`packages/extension/hphp/package.json`) hat eine
eigene Versionsnummer; erhöhe sie, wenn sich die Extension ändert.

## Was dieses Repo ist

- `src/hphpc/`: der Compiler in C11 (Lexer, Parser, Sema, Codegen).
- `src/hphpc/runtime/`: Laufzeitbibliothek. `scripts/build.sh` packt sie
  über `scripts/genembed.c` in `src/hphpc/runtime_embed.h` (generiert, nicht
  von Hand editieren).
- `docs/`: die Website incl. `playground.html` (läuft auf hphp.ch).
- `packages/extension/hphp/`: die VS-Code-Extension samt VSIX.
- `packaging/`: Windows-Installer (Inno), `.deb`/AppImage, winget-Manifeste.
- `tools/playground-api/`: API und Live-Runner hinter dem Playground.
- `scratch/`: Ablage für Wegwerf-Dateien (wird von den Checks ignoriert).

## Kommandos

| Was | Befehl |
|---|---|
| Zustand prüfen | `bash scripts/check-repo.sh` |
| Compiler bauen | `bash scripts/build.sh` → `hphp.exe` (Windows/MinGW) bzw. `hphp` |
| Tests | `bash tests/run.sh` |
| Version prüfen / setzen | `bash scripts/set-version.sh --check` / `<x.y.z>` |
| VSIX neu + installieren | `bash scripts/vsix.sh` |
| **Ausliefern** | `bash scripts/ship.sh --version <x.y.z>` |
| Nur deployen (Notfall) | `bash scripts/deploy-vps.sh` |

## Server

Adresse und URLs stehen **nur** in `scripts/vps.env` (`HPHP_VPS_HOST`).
Ändert sich der Server, wird nur diese Datei angepasst; `check-repo.sh`
meldet jede Adresse, die woanders steht.

| Pfad | Inhalt |
|---|---|
| `/opt/hphp` | Compiler-Checkout; `hphp` wird von Registry **und** Playground genutzt |
| `/opt/hphp-playground/api/index.php` | Playground-API (PHP, `127.0.0.1:8091`, `POST /api/run`, `/api/check`) |
| `/opt/hphp-playground/api/live.py` | Live-Runner (Python, `127.0.0.1:8093`, pty + SSE) |
| `/etc/systemd/system/hphp-playground-live.service` | Unit des Live-Runner (liegt im Repo unter `tools/playground-api/`) |
| `/var/www/hphp.ch` | Website incl. `playground.html`; `dl/` enthält Installer |
| `/etc/nginx/sites-enabled/hphp.ch` | `location /api/live/` → 8093 (ohne `proxy_buffering`!), `location /api/` → 8091 |
| Services | `hphp-playground.service`, `hphp-playground-live.service`, `hphp-registry.service` |

## Playground

- **Live (der Normalfall):** `docs/playground.html` startet pro Klick auf Run
  eine Sitzung: `POST /api/live/start {code, filename}` → `{sid}`, dann
  `GET /api/live/events?sid=` (SSE: `out`-Chunks und `end`),
  `POST /api/live/input {sid, data}` für jede geantwortete `input()`-Zeile,
  `POST /api/live/stop {sid}` zum Abbrechen. Das Programm läuft in einem pty,
  deshalb kommt die Ausgabe zeilenweise, und ein Tastendruck landet sofort im
  laufenden Programm. Limits: **90 s Idle** (jede Ausgabezeile und jede
  Eingabe setzt den Timer zurück) und **300 s Gesamtlaufzeit**, hart killend.
  Der Client spiegelt das (`bumpWatchdog()`) mit 100 s / 320 s, damit der
  Server zuerst meldet. Bei Verstößen `429`, bei unbekanntem `sid` `410` (das
  Signal für EventSource, nicht weiter zu reconnecten).
- **Klassisch (ohne pty):** `POST /api/run` erwartet `{code, filename, stdin}`;
  `stdin` ist ein String, eine Zeile pro `input()`-Aufruf (max. 8 KiB). Ohne
  `stdin` bekommt das Programm eine leere Datei, und `input()` liefert sofort `""`.
- Die Ausgabe kommt mit echten ANSI-Farben zurück; `docs/playground.html`
  übersetzt sie clientseitig nach HTML (`ansiToHtml`).
