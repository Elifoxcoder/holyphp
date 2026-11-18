# Aufräumarbeiten — HolyPHP Directory-Struktur

- src/ (Compiler-Quellcode, hphpc/ und weitere Tools)
- tools/ (holybrowser, holyide, webview2)
- packages/ (extension/, lib/ — eingebettete Bibliotheken)
- tests/ (positive, negative, manual, run.sh)
- benchmarks/ (maxout, 1brc.*, bench1.rs, benchmap.rs)
- docs/ (HTML, guide/, builtin/, assets/)
- scripts/ (build.sh, *.js, *.c, *.ps1)
- packaging/ (linux, windows, winget)
- registry/ (server.hphp, store/)
- scratch/ (temp probes, patch-Skripte, Cargo-Projekte)

Root-Dateien (bleiben oben):
- LICENSE
- Makefile
- README.md
- STATUS.md
- TODO.md

---

## Was wurde gemacht:

- `src/` → `store/src/` angelegt, dann nach `src/` zurück (Compiler-Source)
- `build/` → `store/build/` angelegt, dann nach `build/` zurück (Build-Artefakte)
- `lib/` → `store/pkg/lib/` angelegt, dann nach `packages/lib/` (eingebettete Bibliotheken)
- `extension/` → `store/packages/extension/` (eingebettete Erweiterungen)
- `holybrowser/`, `holyide/`, `webview2/` → `tools/` (Desktop-Tools)
- `extension/`, `lib/` → `packages/` (eingebettete Bibliotheken)
- `packaging/` → `packaging/` (Build-Skripte, Linux/Windows/WinGet)
- `registry/` → `registry/` (Registry-Server)
- `scratch/` → `scratch/` (temp-Probier-Skripte)
- `store/` wurde aufgeräumt und leer gelassen; `store/` wurde am Ende entfernt

---

## Noch zu erledigen:

- [ ] Übernimm die bestehende `README.md` und aktualisiere die Verzeichnisstruktur
- [ ] Teste, ob `bash scripts/build.sh` noch funktioniert (nach der Umsortierung)
- [ ] Prüfe, ob `hphp check` und `hphp run` noch die korrekten Pfade finden

---

## Sinnvolle Unterordner (die noch fehlen / sinnvoll wären):

- `src/lib/` — Bibliotheken für den Compiler
- `src/runtime/` — Laufzeit-Environment, Embed-Code
- `src/tests/` — Compiler-Unit-Tests
- `tools/holybrowser/` — Browser-basiertes IDE-Frontend
- `tools/holyide/` — IDE für HolyPHP-Entwicklung
- `packages/lib/` — Bibliotheken (`ui`, `html`, `ide`, `thread`, `async`, `websocket`)
- `packages/extension/` — Erweiterungen für den Compiler
- `tests/positive/`, `tests/negative/`, `tests/manual/` — Test-Suite
- `benchmarks/maxout/`, `benchmarks/1brc/` — Benchmarks
- `docs/guide/`, `docs/builtin/` — Dokumentation
