# HolyPHP for Visual Studio Code

Language support for `.hphp` powered by a real Language Server Protocol process and the HolyPHP compiler.

## Language tools

- LSP completion for builtins, constants, local declarations, members, types, and keywords; function completions insert argument snippets.
- Hover and signature help. Builtin documentation includes parameter notes, return behavior, runnable examples, expected output, and related functions where the reference provides them.
- Document symbols, workspace symbols, go to definition, find references, and rename across project files.
- On-demand project indexing lets definition, reference, and workspace-symbol queries include closed `.hphp` files. `holyphp.maxIndexedFiles` limits the scan.
- Quick fixes suggest a uniquely close builtin/function, variable, or local type name. Document formatting safely normalizes indentation and trailing whitespace.
- Import completion suggests relative `.hphp` modules in the current workspace.
- Live compiler diagnostics run `hphp check` against the current buffer. Checks are debounced and stale results are discarded.
- `HolyPHP: Check current file`, `HolyPHP: Run current file`, and `HolyPHP: Browse Built-in Reference` commands.
- TextMate highlighting and starter snippets.

The builtin reference is generated from `scripts/builtin-examples.js` and `docs/builtin/*.html`, which are the authored sources for each builtin page. All 426 compiler builtins have a signature and summary; 308 provide structured return notes and examples, while interactive, blocking, and unsafe functions may omit runnable samples. Compiler names come from the compiler's builtin table. Regenerate bundled metadata with `node scripts/genext.js` from the repository root.

## Setup

Install the HolyPHP compiler and ensure `hphp` is on `PATH`, or set `holyphp.hphpPath` to its executable. Diagnostics can be disabled or limited to file saves with `holyphp.diagnostics` and `holyphp.diagnosticsMode`.

## Commands

Open the Command Palette (`Ctrl+Shift+P`) and search for `HolyPHP`. The builtin browser filters by name, signature, and summary, then opens a full Markdown page for the selected function.

## Language server

The bundled `server.js` speaks LSP over stdio and serves completion, hover, signature help, document/workspace symbols, definition, references, rename, quick fixes, and indentation formatting. Project files are indexed lazily on the first workspace query; the scan limit is configurable. Compiler-backed diagnostics and run/check commands are hosted by the VS Code client so they can use the editor's compiler setting and status bar.
