# HolyBrowser

A browser written in HolyPHP. The page is real Chromium (CEF); the chrome
around it — tab strip, toolbar, address bar, settings page — is drawn by
HolyPHP's own UI library, in HolyPHP source, in this folder.

```
holybrowser/
  holybrowser.hphp        the whole app
  build.sh                compiler -> shim -> app, in that order
  build_cefshim.bat       cefshim.dll + CEF runtime staging (MSVC)
  native/cefshim.cpp      the plain-C ABI over CEF that the runtime loads
```

## Build

```sh
bash holybrowser/build.sh
./build/holybrowser.exe
```

Three stages, and the order matters:

1. `scripts/build.sh` regenerates `src/hphpc/runtime_embed.h` from
   `src/hphpc/runtime/*.c` and links it into `hphp.exe`. Any change under
   `runtime/` has to go through this before the app is built, or the app is
   linked against code that no longer exists.
2. `build_cefshim.bat` compiles `cefshim.dll` with MSVC and stages the CEF
   binaries into `build/cef_runtime/`. It runs through `cmd` on purpose: MSYS
   rewrites `/flags` into paths. **Both `COMPILE_EXIT` and `STAGE_EXIT` must be
   0.** (It runs the compiler twice to check the exit code; that is deliberate,
   not a mistake — an `| more` in that pipeline used to swallow the status.)
3. `hphp build holybrowser/holybrowser.hphp -o build/holybrowser.exe --gui`.
   `--gui` links the Windows GUI subsystem; without it every launch leaves a
   console window open behind the browser.

**Kill `holybrowser.exe` before building.** The executable stays locked while it
is open, `hphp build` then fails with `cannot place executable at ...`, and the
old binary keeps running so whatever you test afterwards is not what you built.

The exe must sit beside `build/cef_runtime/`, or `HOLYCEF_SHIM` must point at a
`cefshim.dll` there. Without the engine the app says so in a window instead of
starting blank.

## Where things live

Settings, history and the profile are written under `%APPDATA%\hphp\`:

| file | what |
| --- | --- |
| `holybrowser.json` | search engine, home page, theme, zoom, devtools width, history |
| `holybrowser/tabs/` | Chromium's on-disk cache, one folder per tab |
| `%TEMP%\hphp_favicons/` | decoded site icons, one file per URL |

## Debugging

```sh
HOLYCEF_DEBUG=1        trace the engine: windows, loads, titles, favicons, postMessage
HOLYCEF_DEBUG=2       the same, plus a line per layout fit
HPHP_UI_DEBUG=1        trace WM_COMMAND / control clicks / event callbacks
HPHP_UI_DEBUG=2        draw every toolkit icon in red, to see what is missing
HOLYCEF_START=url      open this URL in the first tab instead of the home page
HOLYCEF_PAGE_DIR=dir   write any page HolyBrowser generated itself to dir/page.html
HOLYCEF_SHIM=path      use a cefshim.dll from somewhere else
```

A page HolyBrowser draws itself is loaded as a `data:` URL, so it is otherwise
only visible as base64 in a log; `HOLYCEF_PAGE_DIR` writes it out as text.
`HOLYCEF_START=holybrowser://settings` is the way to reach an internal page
from a script or a test, since there is no address bar to type into yet.

The render process is a separate executable with its own stderr, so anything
the shim traces also goes to `%TEMP%\hphp_cef_trace.log`, one shared file with
each line prefixed `[pid N]`. A line missing from the browser's console is not
evidence that the render process did not print it.

`build/probe.ps1` drives the running app with a real, DPI-correct mouse and
prints the client rectangle of every control — the quickest way to check where
something landed. It is a scratch tool, not part of the app.

## Known issues

- Nothing outstanding. The DevTools docking, the tab close button and the
  settings round trip are all verified working; `holybrowser://settings` now
  applies a theme on change and writes `holybrowser.json` on Save.