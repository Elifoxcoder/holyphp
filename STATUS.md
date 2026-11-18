# Status

_Last updated: 2026-10-17._

Two questions this file answers: what state the repository is in, and how far
HolyIDE is from being an editor you could use for real work.

---

## 1. Repository state

The tree was reorganised so that each kind of file has exactly one home and one
source of truth.

| Area | Before | Now |
|---|---|---|
| Libraries | flat `lib/ui.hphp` **and** a drifting copy in `lib/ui/ui.hphp`; `html`/`ide` only on disk | one package folder per library: `lib/<name>/hphp.json` + `<name>.hphp` (+ parts). All six are embedded — `import "html"` / `import "ide"` work with an installed compiler in a project that has no `lib/` at all |
| Tests | `tests/positive/run_all.sh`, a docs checker hidden in `positive/`, self-tests in `examples/` | `tests/run.sh` (positive + negative + docs), `tests/manual/` for window/socket/thread tests, `tests/check_docs.sh` |
| Examples | 30 files: probes, generated C (71 KB), three `.exe`s, a German duplicate tour, a game | 13 curated programs + `bench`/`record` C twins |
| Docs | 12 hand-written pages, stale counts (235 builtins, 5 libraries), examples of deleted files | one site in `docs/`, new `html` and `ide` pages, current counts, `examples.html` matches the tree |
| Junk | CEF profile (`holybrowser/cefdata`), `build/`, crash probes (`lib/probe_*.hphp`), six one-off `fix_examples*.py`, `registry/server.log` | removed; `.gitignore` covers browser profiles, build output and logs |

Build output stays out of the tree: `build/`, `*.exe`, `holybrowser/cefdata/`.

### Verified

```
bash scripts/build.sh              → build ok: hphp.exe (runtime embedded)
bash tests/run.sh                  → 29 passed, 0 failed, 0 skipped
bash tests/run.sh manual           → window / socket / thread self-tests
./holyide/holyide.exe --selftest   → toolbar buttons are boxes, pane border,
                                     no narrow text rect, sidebar bg, status bg,
                                     title bg, bottom: ok
./build/holyhtml_demo.exe          → pixels ok, frames=60 paints=61 relayouts=62
```

`tests/run.sh` runs 19 positive programs, 9 negative ones and 21 documentation
snippets. `tests/positive/ide_workbench.hphp` alone makes 167 assertions and
`ide_editor.hphp` 76, out of 393 in the positive tests.

### The first real session, and what it broke

HolyIDE was opened on a 125 % display and did not survive contact: no
shortcut worked, no file opened, every label was cut short, the terminal
ignored the keyboard, pressing a letter typed its ASCII code, panes could not
be resized and nothing was selected with the mouse. Ten symptoms, four root
causes, all of them now covered by a test:

- **An inline span inside a flex container was not blockified.** The whole
  title bar, the status bar and every tab's close glyph were merged into one
  run of anonymous text: no boxes, so no padding, no background, no click
  target and no ellipsis to stop at. That is why no button could be clicked
  and why labels were cut off (`layout.hphp`, `build_flex`).
- **A key press delivered its code point as a number.** The handler is written
  `key($vk, $ch, $mods)` and inserts `$ch`, so typing `a` inserted "97". The
  character now travels as a string, and Ctrl/Alt combinations send an empty
  one - except Ctrl+Alt, which is AltGr and has to keep typing `@` and `{`
  (`hphp_ui.c`, `cv_fire_key`).
- **`position: absolute` children were never measured.** Every popup - the
  palette, the completion list - was a zero-by-zero box that painted nothing,
  which is why `Ctrl+P` looked like a key that did nothing. Out-of-flow boxes
  are sized now and positioned by `place_abs()` (`layout.hphp`).
- **Text widths were rounded to nearest while the drawing rectangle was
  truncated.** At 125 % a string measured 31.25 device pixels, the rect got 31,
  and GDI dropped the last glyph - "Save" was rendered "Sav" everywhere. Widths
  round up now, a plain draw gets one pixel of slack, and the painter no longer
  clamps a line's last atom to "whatever is left of the box".

Two more fell out of the same session: the stylesheet was cached while it
contains the pane sizes and the font size (so a dragged grip laid the document
out at the old size), and the wheel moved three lines a notch, which is not
scrolling but jumping.

### Fixes that fell out of the cleanup

Things that were broken and would have blocked HolyIDE:

- **Libraries could not be imported together.** `ui` and `html` both defined a
  class `Layout`; a program has one namespace, so an HTML view inside a `ui`
  window silently used the wrong one. The layout engine is `HtmlLayout` now.
- **`import "html"` / `import "ide"` failed on an installed compiler**, because
  only `lib/*.hphp` was embedded and the resolver only knew flat libraries.
  Embedding now keeps the path inside `lib/` (`html/dom.hphp`), the resolver
  looks in `lib/<name>/<name>.hphp`, and `hphp pkg install <lib>` stages the
  whole folder, not just the entry file.
- **`flex-basis` was parsed and ignored.** `flex: 1` started each item at its
  content size instead of 0, so a column with 1 px too much content pushed its
  last child off-screen (the IDE's status bar sat one pixel below the window).
  Basis is honoured now, and a box that receives a height from its flex parent
  re-runs its own distribution.
- **A window resize left the layout stale.** The workbench only re-parsed the
  document when its HTML changed, never when the viewport did. It now caches
  the size along with the document.
- **Two logical-pixel conversions disagreed** (`cv_unscale` truncated,
  `ui_client_size` rounded), so a canvas reported 784 px in a 783 px window at
  non-100% DPI.

---

## 2. How close is HolyIDE?

`holyide/holyide.hphp` runs. Explorer, tabs, an editor with undo/redo and
completion, a command palette, a terminal with the runner, a problems list, a
status bar, resizing and a caret all work, and the window app has a pixel
self-test.

**Distance to a usable daily driver for HolyPHP: roughly 70%.**
**Distance to a general-purpose IDE: roughly 35%** — the missing half is the
ecosystem around the editor (search, git, settings, extensions, debugging).

### Done

| Area | State |
|---|---|
| Text editing | insert/delete, newline with auto-indent, backspace that joins lines, Tab/Shift+Tab, Alt+↑/↓ move, Ctrl+D duplicate, Ctrl+/ comment, word/page/doc movement, bracket matching |
| Selection | shift+arrows, select-all, mouse click and drag, double-click for a word, gutter click for a line, `first_end`/`last_end`, cut/copy/paste through the system clipboard |
| History | line-slice undo/redo, coalesced per typing run, 500 steps |
| Syntax | line tokenizer (keywords, types, functions, variables, strings, numbers, comments), `/* */` carried across lines |
| Completion | vocabulary from builtins + file words, 2-character threshold, `Ctrl+Space` on demand, popup with keyboard and mouse selection |
| Files | explorer that folds (folders, 800-entry/6-level cap, build dirs skipped), tabs with dirty dot and a clickable close, save, save-all, revert, new untitled buffer |
| Navigation | Ctrl+P files, Ctrl+Shift+P commands, Ctrl+G line, click to caret, one line a notch with the wheel, tree and code scrolling with their own step |
| Running | `hphp run <file>` in the file's folder, live stdout/stderr in the terminal panel, exit code, problem parsing with click-to-open, and a terminal command line (Ctrl + backtick) that runs through `cmd /c` with history |
| Chrome | title bar with run/stop, panel tabs (terminal/problems), status bar, zoom, and draggable pane borders (explorer and panel) |
| Testing | 393 headless assertions plus a canvas pixel self-check that also proves the toolbar has boxes and no string is drawn too narrow |

### Missing (in rough priority order)

1. **Diagnostics while typing.** Problems only appear after a run. Needs a
   debounced background `hphp check`, cancellation of the previous one, and
   squiggles/gutter marks.
2. **Search and replace across files.** A project-wide find is the single
   biggest gap for daily use; `ide_scan_tree` already gives the file list.
3. **Session persistence.** Window size, open folder, tabs, caret positions and
   unsaved buffers die with the process.
4. **Save-as / rename / delete / new file from the explorer.** The tree is
   read-only; there is no dialog for a path without one.
5. **File watching.** External edits are only noticed on revert.
6. **Tab overflow.** More tabs than fit in the strip run off the edge; a scroll
   or an overflow menu is needed.
7. **Split editors.** One editor per window; the pane borders drag, the editor
   itself does not split.
8. **Git.** No status decoration, diff view, branch display, or commit — not
   even plumbing.
9. **Settings.** No file, no UI; font size and theme are code constants.
10. **Theming.** Only `html_theme_dark()` is wired; a light theme exists in the
    library but the app does not switch to it.
11. **Multi-cursor, column selection, code folding** — quality-of-life, none of
    it started.
12. **Debugger, extensions, LSP, tasks** — the ecosystem layer. The panel's
    command line runs a command and shows its output; it is a pipe, not a TTY
    (no interactive programs, no colours, no cursor addressing).
13. **Packaging.** No app icon, no installer entry, no "open with" integration,
    no file association for `.hphp`.

### Roadmap

**0.2 — daily driver** (the first four gaps): background `hphp check` with
squiggles, project search/replace, session restore, save-as/rename/delete, file
watching, tab overflow.
**0.3 — workspace**: split editors, settings file + UI, light theme, git status
in the status bar and a read-only diff. (Draggable panes are done.)
**1.0 — beyond the editor**: debugger (breakpoints over a `hphp` debug build),
an extension point, and an installer that registers `.hphp`.

Each milestone should keep the current rule: the model and the chrome are a
pure state machine rendered to HTML, tested headlessly; the window stays a thin
shell.

---

## 3. Known limitations that are not IDE work

- **Argument evaluation order.** Generated C evaluates call arguments
  right-to-left, so `f() . count($x)` may read `$x` before `f()` runs. PHP is
  left-to-right. Not fixed; avoid relying on side-effect order inside one
  expression.
- **Windows-only desktop.** `ui`, `html` painting and HolyIDE need Win32. The
  libraries still type-check and the headless halves still run on Linux, and
  `hphp` builds there (the UI layer compiles to inert stubs).
- **`flex-shrink` is not implemented.** A toolbar or a status bar whose items
  are wider than the window still runs off the right edge and is clipped, where
  a browser would shrink the items down to their minimum content. Grow is
  distributed; shrink is not.
- **Scrolling is per line, not per pixel.** The pane scrolls one row a notch,
  which is smooth for reading and is what a text pane does; there is no
  fractional scroll offset, so a trackpad's fine-grained scroll arrives as whole
  lines.
- **Documentation snippets** are checked by `tests/check_docs.sh` only when a
  Python interpreter is available; without one the suite reports SKIP.
