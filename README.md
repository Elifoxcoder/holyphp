# HolyPHP

**A compiled, memory-safe programming language with the soul of PHP and the discipline of Rust.**

HolyPHP compiles `.hphp` source files to native executables via C. You keep what makes PHP pleasant: `$variables`, `echo`, `foreach`, arrays with string keys, string interpolation and the familiar built-in functions. HolyPHP adds what PHP never had: a compile-time borrow checker, explicit ownership (`own<T>`), optional static types, and raw pointers allowed only inside `unsafe`.

```php
// hello.hphp — no imports, no main(), just start writing
$name = "world";
echo "Hello, {$name}!\n";

// types are optional — add them when you want guarantees
function fib(int $n): int {
    if ($n < 2) { return $n; }
    return fib($n - 1) + fib($n - 2);
}
echo "fib(10) = " . fib(10) . "\n";
```

```bash
hphp run hello.hphp
```

Website, documentation and an online playground: **[hphp.ch](https://hphp.ch)**

## Highlights

- **Feels like PHP.** `$variables`, `echo`, `foreach`, associative arrays, string interpolation, loose typing when you want it. More than 350 PHP-style built-in functions, all global with no imports needed ([reference](https://hphp.ch/builtins.html)).
- **Safe like Rust.** Borrows are checked at compile time: many immutable borrows xor exactly one mutable borrow, and use-after-move is rejected. Ownership is explicit with `own<T>`, and raw pointers exist only inside `unsafe { }`.
- **Types when you want them.** `$x = 5` just works. `$x: int = 5` pins the type down, and typed code compiles to tighter C.
- **Native binaries.** `hphp build` produces a standalone executable. The runtime is linked in, so the target machine needs nothing else.
- **One-file toolchain.** The compiler embeds the whole runtime and the standard libraries.
- **Batteries included:**
  - `ui`: native desktop GUI with themes, cards, forms and lists, laid out automatically.
  - `websocket`: an RFC 6455 server; browsers connect out of the box.
  - `thread`: OS threads with Go-style channels, atomics and barriers.
  - `async`: a cooperative event loop with timers, sockets and HTTP.
- **Packages.** `hphp pkg init / publish / install`, with a public registry that is itself written in HolyPHP.
- **Editor support.** A VS Code extension with highlighting, completion for every built-in, hover signatures and live diagnostics from `hphp check`.

## Install

| Platform | How |
|---|---|
| Windows | `winget install hphp`, or the installer from [Releases](https://github.com/Elifoxcoder/holyphp/releases/latest) (no admin rights needed) |
| Debian / Ubuntu | `.deb` from [Releases](https://github.com/Elifoxcoder/holyphp/releases/latest): `sudo apt install ./holyphp_<version>_amd64.deb` |
| Other Linux | AppImage from [Releases](https://github.com/Elifoxcoder/holyphp/releases/latest) |

HolyPHP uses a C compiler as its backend, so `hphp run` and `hphp build` need **gcc**:
- Linux: `sudo apt install build-essential`
- Windows: MinGW, for example via MSYS2

The installers check for gcc and tell you how to get it.

## Usage

```bash
hphp check app.hphp               # type + borrow check only (fast feedback)
hphp run   app.hphp               # compile and run
hphp build app.hphp -o app.exe    # standalone executable
hphp emit  app.hphp               # print the generated C
hphp pkg install ui               # fetch a library from the registry
```

All commands and environment variables are listed on the [CLI page](https://hphp.ch/cli.html).

## A taste of the language

**Ownership and borrows**, checked at compile time:

```php
function bump(int &$r): void { $r += 1; }   // mutable borrow
$n = 41;
bump($n);                                    // $n is now 42

unsafe {                                     // raw pointers only in here
    $p = &$n;
    echo "deref: " . (*$p) . "\n";
}
```

**A desktop GUI** in a few lines:

```php
import "ui";

$app = new App("My App", 560, 300);
$app->theme("midnight")->pad(24)->gap(10)->radius(12);
$name = $app->input("world");
$app->button("Click me", "primary", function() use ($app, $name) {
    $app->msg("Hello, " . $name->content() . "!");
});
$app->run();
```

More examples are in [`examples/`](examples/). The full language guide is at [hphp.ch](https://hphp.ch/lang.html).

## Building from source

```bash
bash scripts/build.sh      # builds ./hphp(.exe) with the runtime embedded
bash tests/run.sh          # runs the test suite
```

The pipeline is lexer → recursive-descent parser → semantic analysis (types, borrows, ownership) → C code generation → gcc. The runtime is written in C and embedded in the compiler. It covers values, strings, copy-on-write arrays, exceptions, sockets, `io_uring`, SQLite and the GUI. At build time the runtime is extracted, compiled and linked statically.

## Repository layout

| Path | Contents |
|---|---|
| `src/hphpc/` | Compiler (lexer, parser, sema, codegen) |
| `src/hphpc/runtime/` | Runtime library embedded into every program |
| `lib/` | Standard libraries (`ui`, `websocket`, `thread`, `async`) |
| `extension/` | VS Code extension |
| `docs/` | Website and documentation (hphp.ch) |
| `examples/` | Example programs |
| `tests/` | Test suite |
| `packaging/` | Windows installer, `.deb`, AppImage, winget manifests |
| `registry/` | Package registry server |
| `benchmarks/` | Benchmarks and comparisons |

## Performance

HolyPHP is built for real workloads. In the community [Twelve Dollar Server Challenge](https://github.com/arjaythedev/twelve-dollar-server-challenge) (one HTTP API on a 1 vCPU / 2 GB VPS, scored by how many users a server holds for five minutes), the HolyPHP submission currently holds **260,000 users**.¹ That puts it among native entries written in C, Rust and Go.

---

¹ Official benchmark of PR [#447](https://github.com/arjaythedev/twelve-dollar-server-challenge/pull/447): 260,000 users held for 5 minutes, p95 154 ms, p99 659 ms, 0 errors. See the [leaderboard](https://github.com/arjaythedev/twelve-dollar-server-challenge/blob/main/LEADERBOARD.md) and the [submission](https://github.com/arjaythedev/twelve-dollar-server-challenge/tree/main/submissions/holyphp-io-uring-elifoxcoder). An older head-to-head comparison against a Rust server on the same VPS is in [`benchmarks/maxout-vs-rust.md`](benchmarks/maxout-vs-rust.md).

## License

[MIT](LICENSE) © 2026 Elias Fuchs
