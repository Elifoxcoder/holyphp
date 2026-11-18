# HolyPHP VS Code Extension

Syntax-Highlighting, Autovervollständigung, Hover, Signature Help, Symbole und
**Live-Fehlerdiagnose** für **HolyPHP** (`.hphp`) in Visual Studio Code.

## Features

- **Syntax-Highlighting** – PHP-ähnliche Grammatik: `$variablen`, Keywords,
  Typen (`int`, `u8`, `string`, `Vec`, …), Strings mit `{$interpolation}`,
  Zahlen (hex, binär, `1_000_000`), Operatoren (`<=>`, `??`, `**`, …).
  Die Builtin- und Konstanten-Listen werden aus der Compiler-Tabelle
  generiert und können nicht mehr veralten.
- **Autovervollständigung** (Strg+Leertaste), kontextabhängig:
  - nach `->` / `::`: **Member der Klasse** des Empfängers (inkl. `extends`),
  - nach `new`: Klassen der Datei,
  - an Typstellen (Parameter, `let $x:`, `extends`/`implements`): Typen,
  - sonst: `$variablen`, eigene Funktionen (als Snippet mit Parametern),
    alle **426 Builtins** (Signatur + Beschreibung, `unsafe` markiert),
    Konstanten mit Wert, Keywords und Typen.
- **Hover**: Signatur + Beschreibung bei Builtins, Konstanten (mit Wert),
  eigenen Funktionen, Klassen (mit Membern), Keywords und `$variablen`
  (deklarierter Typ, Zeile, Anzahl Verwendungen).
- **Signature Help**: Beim Tippen von `(` / `,` werden die Parameter des
  Builtins oder der eigenen Funktion angezeigt, der aktive Parameter
  hervorgehoben.
- **Symbole / Outline**: Funktionen, Klassen, Methoden, Felder, Konstanten und
  Imports – für Gliederung, Breadcrumbs und „Gehe zu Symbol".
- **Gehe zu Definition** (gleiche Datei) für Funktionen und Klassen.
- **Live-Diagnose**: `hphp check` läuft beim Tippen (entprellt) oder beim
  Speichern und zeigt Fehler als Squiggles. Geprüft wird **der Editor-Puffer**,
  nicht der Stand auf der Platte: ungespeicherte Änderungen werden über eine
  temporäre Datei neben dem Original geprüft (relative `import`s bleiben
  gültig). Auch Syntaxfehler (`hphpc: error: …`) werden zugeordnet, Fehler in
  importierten Dateien landen in der jeweiligen Datei.
- **Befehle**:
  - `HolyPHP: Check current file` (Strg+Umschalt+M)
  - `HolyPHP: Run current file` (Strg+F5) – führt die Datei im Terminal aus
- **Snippets**: `fun`, `fore`, `forek`, `switch`, `class`, `try`, `matchx`, …
  (z. B. `fun` + Tab erzeugt eine typisierte Funktion)

## Installation

### VSIX (empfohlen)

Die fertige Paketdatei liegt im Repo:

```bash
code --install-extension packages/extension/hphp/holyphp-0.1.2.vsix
```

Danach VS Code einmal neu starten. Jede `.hphp`-Datei wird automatisch
als HolyPHP erkannt (unten rechts: „HolyPHP").

### Voraussetzungen

- Der Compiler `hphp` muss im `PATH` sein (oder Pfad in den Einstellungen
  unter `holyphp.hphpPath` angeben), sonst keine Diagnose. Fehlt er, zeigt die
  Statusleiste `hphp not found` mit einem Knopf zu den Einstellungen.

## Einstellungen

| Einstellung | Default | Bedeutung |
|---|---|---|
| `holyphp.hphpPath` | `"hphp"` | Pfad zum Compiler |
| `holyphp.diagnostics` | `true` | Diagnose an/aus |
| `holyphp.diagnosticsMode` | `"type"` | `"type"` (live) oder `"save"` |
| `holyphp.diagnosticsDebounce` | `300` | Wartezeit nach dem letzten Tastendruck (ms) |

## Tests

Die Extension hat eine eigene Testsuite (reines Node, ohne VS Code und ohne
npm-Abhängigkeiten):

```bash
cd packages/extension/hphp
node --test test/*.test.js
```

Abgedeckt sind: Compiler-Ausgabe → Diagnose (alle Formate, Windows-Pfade,
Zeilen/Spalten, Bereiche), Signatur-Parsing, Completion-Kontexte,
Symbol-Erkennung, Hover, Signature Help, Grammatik-Abdeckung, Manifest-Pfade,
das Laden der Extension mit einem `vscode`-Stub – und Integrationstests gegen
den echten `hphp check` (werden übersprungen, wenn kein Compiler im `PATH`
liegt). `bash tests/run.sh ext` und `bash scripts/vsix.sh` führen sie mit aus.

## Für Entwickler: Builtins aktualisieren

`data/builtins.json` **und** die Builtin-/Konstanten-Regexe der Grammatik
kommen aus der Compiler-Tabelle `src/hphpc/builtins.c`. Nach dem Hinzufügen
neuer Builtins im Compiler:

```bash
node scripts/genext.js          # builtins.json + syntaxes/hphp.tmLanguage.json
bash scripts/vsix.sh            # genext + Tests + VSIX + Installation
```

## Warum keine PHP-Extension angepasst wurde

Die offizielle PHP-Extension zieht `php-language-server` nach und erwartet
echtes PHP. HolyPHP hat andere Semantik (Borrow-Checker, `$a[] = x`,
eigene Diagnose über `hphp check`) – deshalb eine schlanke, eigene
Extension mit demselben Look & Feel, aber gegen den **echten**
HolyPHP-Compiler geprüft.
