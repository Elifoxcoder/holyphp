# Examples

Runnable programs, one idea each. `docs/examples.html` is the same list with
excerpts and explanations.

```bash
hphp run examples/hello.hphp
```

| File | What it shows |
|---|---|
| `hello.hphp` | the classic one-liner |
| `demo.hphp` | the language tour: functions, arrays, classes, closures, match, exceptions |
| `ui_demo.hphp` | UI 2.0: themes, cards, forms, lists, meters, menus, timers |
| `ui_gallery.hphp` | every theme and every control |
| `canvas_demo.hphp` | a raw canvas: draw calls, pointer and keyboard events as numbers |
| `holyhtml_demo.hphp` | an HTML/CSS surface painted by HolyPHP, verified by pixel probes |
| `ws_chat.hphp` + `ws_chat.html` | a browser chat server over RFC 6455 |
| `device_feed.hphp` | a WebSocket feed of smart-home JSON, one update per second |
| `async_demo.hphp` | timers, TCP servers and HTTP in one event loop |
| `thr_demo.hphp` | channels, atomics, barriers, parallel map |
| `websocket_demo.hphp` | the WebSocket API without a browser |
| `bench.hphp` / `bench.c` | micro-benchmark and its hand-written C twin |
| `record.hphp` / `record.c` | int/float loop benchmark and its C twin |

The IDE lives in `holyide/` (it is an app, not an example):

```bash
hphp run holyide/holyide.hphp
```

Tests that need a window, a port or threads are in `tests/manual/`.
