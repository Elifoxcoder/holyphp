# HolyPHP package registry

A tiny HTTP/1.1 registry server written in HolyPHP itself (`server.hphp`).
It is what `hphp pkg install <name>` / `hphp pkg search` talk to.

## Run it

```bash
cd registry               # the store/ folder is resolved relative to the cwd
hphp run server.hphp      # listens on http://localhost:8930
```

The server stores packages in `./store/` (versioned + latest `.hpx` and a
`.json` metadata file per package). **Start it from this directory** — there
is no `__DIR__` in HolyPHP yet, so a different cwd would create/use another
`store/` folder.

## Point the client at it

```bash
hphp pkg registry http://localhost:8930    # once, saved to the pkg home
hphp install websocket                 # then install away
hphp pkg search
```

## Publish a package

A package is a folder with `hphp.json` + sources (see `lib/websocket/` or
`lib/ui/` for real examples):

```bash
cd lib/websocket
hphp pkg publish
```

`publish` packs the folder into `<name>-<version>.hpx` (same as
`hphp pkg pack`) and POSTs it to the configured registry.

## What ships pre-published

The bundled `store/` comes with:

| package     | version | what it is                                             |
|-------------|---------|--------------------------------------------------------|
| `websocket` | 1.0.0   | RFC 6455 WebSocket server (`lib/websocket/`)           |
| `ui`        | 1.0.0   | native Win32 UI toolkit (`lib/ui/`)                    |
| `mathx`     | 1.0.0   | tiny math helpers                                      |

After `hphp install websocket`, `import "websocket.hphp";` in any
script resolves to the installed copy — see `examples/websocket_demo.hphp`.

## HTTP API

```
GET  /                 index of published packages (plain text)
GET  /search?q=word    plain-text search listing
GET  /pkg/<name>       the latest .hpx for <name>
POST /publish          body = .hpx archive (parsed for hphp.json metadata)
```

No TLS, no auth — this is a single-user / LAN registry by design.
