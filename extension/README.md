# HolyPHP VS Code Extension

Syntax-Highlighting, Autovervollständigung, Hover und Live-Fehlerdiagnose
für **HolyPHP** (`.hphp`) in Visual Studio Code.

## Features

- **Syntax-Highlighting** – PHP-ähnliche Grammatik: `$variablen`, Keywords,
  Typen (`int`, `string`, `array`, …), Strings mit `{$interpolation}`,
  Zahlen, Operatoren (`<=>`, `??`, `**`, …)
- **Autovervollständigung** (Strg+Leertaste):
  - alle **182 Builtins**, automatisch aus der Compiler-Tabelle generiert –
    mit Signatur und Beschreibung (`sort(array &$a): bool`, …)
  - eigene **Funktionen** inkl. Parameterliste als Snippet
  - **`$variablen`** im Dokument, Klassen, Keywords, Typen
  - Member nach `->` und `::`
- **Hover**: Signatur + Beschreibung bei Builtins und eigenen Funktionen
- **Live-Diagnose**: `hphp check` läuft beim Speichern (oder bei jedem
  Tastendruck, einstellbar) und zeigt Compiler-Fehler als rote
  Squiggles mit Zeile/Spalte
- **Befehle**:
  - `HolyPHP: Check current file` – Typcheck der aktuellen Datei
  - `HolyPHP: Run current file` (Strg+F5) – führt die Datei im Terminal aus
- **Snippets**: `fun`, `fore`, `forek`, `switch`, `class`, `try`, …
  (z. B. `fun` + Tab erzeugt eine typisierte Funktion)

## Installation

### VSIX (empfohlen)

Die fertige Paketdatei liegt im Repo:

```bash
code --install-extension extension/hphp/holyphp-0.1.0.vsix
```

Danach VS Code einmal neu starten. Jede `.hphp`-Datei wird automatisch
als HolyPHP erkannt (unten rechts: „HolyPHP").

### Voraussetzungen

- Der Compiler `hphp` muss im `PATH` sein (oder Pfad in den Einstellungen
  unter `holyphp.hphpPath` angeben), sonst keine Diagnose.

## Einstellungen

| Einstellung | Default | Bedeutung |
|---|---|---|
| `holyphp.hphpPath` | `"hphp"` | Pfad zum Compiler |
| `holyphp.diagnostics` | `true` | Diagnose an/aus |
| `holyphp.diagnosticsMode` | `"save"` | `"save"` oder `"type"` (live) |

## Für Entwickler: Builtins aktualisieren

Die Autovervollständigung kommt aus `data/builtins.json`, das aus der
Compiler-Tabelle `src/hphpc/builtins.c` generiert wird. Nach dem Hinzufügen
neuer Builtins im Compiler:

```bash
node scripts/genext.js
```

Dann die VSIX neu bauen:

```bash
cd extension/hphp
npx @vscode/vsce package --allow-missing-repository -o holyphp-0.1.0.vsix
code --install-extension holyphp-0.1.0.vsix --force
```

## Warum keine PHP-Extension angepasst wurde

Die offizielle PHP-Extension zieht `php-language-server` nach und erwartet
echtes PHP. HolyPHP hat andere Semantik (Borrow-Checker, `$a[] = x`,
eigene Diagnose über `hphp check`) – deshalb eine schlanke, eigene
Extension mit demselben Look & Feel, aber gegen den **echten**
HolyPHP-Compiler geprüft.
