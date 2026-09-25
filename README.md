# HolyPHP (.hphp)

**HolyPHP is a low-level, compiled programming language with the soul of PHP.**

It looks and feels like PHP — `$variables`, `echo`, `foreach ($arr as $k => $v)`,
`function f(int $n): int`, string interpolation `"Hi, {$name}!"`, and 150+ familiar
standard functions (`strlen`, `array_map`, `json_encode`, …). Under the hood it is a
completely from-scratch toolchain written in C11:

```
source .hphp → lexer → parser → type & borrow checker → native binary
```

The toolchain is **one self-contained `hphp` binary** — like `rustc` or `go`. It embeds
its own runtime, keeps every intermediate artifact in a private build directory
(`%LOCALAPPDATA%\.hphp` / `~/.hphp`), and hands you only a native executable.
Your project directory never fills up with build files. There is **no interpreter and
no VM** — your program compiles straight to machine code.

---

## The memory model (what makes it "low-level but safe")

HolyPHP keeps PHP's easy value semantics but adds modern memory safety:

| Concept | Syntax | Meaning |
|---|---|---|
| Variable | `$x = 10;` | Assignment declares the variable (inferred type) |
| Explicit binding | `let $x = 10;` / `let mut $x = 10;` | Optional, Rust-style declarations |
| Mutable binding | `$x = 10;` then `$x += 1;` | Auto-declared variables are mutable |
| Borrow (read) | `$r = &$x;` | Many readers **or** one writer — enforced at compile time |
| Mutable borrow | `$r = &mut $x;` | Exclusive write access while borrowed |
| Smart pointer | `own<T>`, `Rc<T>` | Owning / reference-counted pointers; no manual free |
| Raw pointer | `*T` (e.g. `$p: *int`) | Only usable inside `unsafe { }` blocks |
| Move semantics | assignments of `own<T>` | Value is moved; use-after-move is a compile error |

```hphp
$a = 5;
$b = &$a;       // $b borrows $a
$a += 1;        // ERROR: cannot assign while $a is immutably borrowed

class Box { pub int $v = 0; }
$b1 = new Box();          // own<Box> by default
$b2 = $b1;                // moved — $b1 is now dead (compile-time enforced)
```

## A quick tour

```hphp
// PHP-style variables: just assign. Types are inferred.
$name = "World";
$count = 10;

// typed functions, exactly like PHP
function fib(int $n): int {
    if ($n < 2) { return $n; }
    return fib($n - 1) + fib($n - 2);
}
echo "fib(10) = " . fib(10) . "\n";

// string interpolation
echo "Hello, {$name}!\n";

// arrays and maps
$nums = [5, 3, 8, 1, 9];
$user = ["name" => "Elias", "lang" => "HolyPHP"];
foreach ($nums as $x) { ... }
foreach ($user as $k => $v) { echo "{$k} => {$v}\n"; }

// classes
class Counter {
    pub int $count = 0;
    pub function bump(): void { $this->count += 1; }
    pub function get(): int   { return $this->count; }
}
$c = new Counter();
$c->bump();

// closures with use ()
$base = 100;
$addBase = function(int $x) use ($base): int { return $base + $x; };
echo $addBase(5);   // 105

// match expressions
function classify(int $n): string {
    return match ($n) {
        0 => "zero",
        1, 2, 3 => "small",
        _ => "big",
    };
}

// exceptions
try {
    throw "boom";
} catch (string $e) {
    echo "caught: {$e}\n";
}

// raw pointers only inside unsafe
unsafe {
    // $p: *int, *$p, casts to raw pointers ...
}
```

## Standard library

HolyPHP ships the PHP functions you know, implemented natively in the runtime
(`src/hphpc/runtime/`): strings (`strlen`, `substr`, `sprintf`, `str_replace`, …),
arrays (`array_map`, `array_filter`, `array_reduce`, `in_array`, …), math (`abs`,
`floor`, `sqrt`, `sin`, …), types (`is_int`, `intval`, `gettype`, …), I/O
(`print_r`, `var_dump`, `readline`, `file_get_contents`, …), encodings
(`json_encode`, `base64_encode`, `md5`, …), and more — 150+ functions.

## Installing / building

Requirements: a C11 compiler (gcc/clang/MSVC) and GNU `ld` on Windows.

```bash
bash scripts/build.sh     # or: make
./hphp.exe run examples/demo.hphp
```

## Installers (MSIX / .deb / AppImage)

Ready-made install wizards live in `packaging/` — they install the compiler
only and put it on the PATH; the `ui` / `websocket` libraries are fetched with
the package manager afterwards (`hphp install ui`).

| Target | Install |
|---|---|
| Windows | `HolyPHP-1.0.0-x64-setup.exe` — a standard install wizard (`powershell -File packaging/windows/make-exe.ps1` to build), or MSIX (`packaging/windows/make-msix.ps1`), or the wizard-only `packaging/windows/Install-HolyPHP.ps1` |
| Debian/Ubuntu | `sudo apt install ./holyphp_1.0.0_amd64.deb` (built by `packaging/linux/make-deb.sh`) |
| Linux (portable) | `HolyPHP-1.0.0-x86_64.AppImage` (built by `packaging/linux/make-appimage.sh`); `./HolyPHP...AppImage install` adds it to PATH |

See `packaging/README.md` for the full matrix and details.

## Installing hphp system-wide (the "real language" experience)

Put `hphp.exe` (Linux/macOS: `hphp`) anywhere on your `PATH`, e.g. `C:/tools/hphp/`:

```bash
hphp run hello.hphp     # or just: hphp hello.hphp
hphp build hello.hphp -o hello
```

That is the whole install. The runtime lives inside the binary; intermediates go
to the private `.hphp` cache directory. Your projects contain only `.hphp` files
and the executables you build.

If `make`/`gcc` linking is flaky on your machine (on-access AV scanners),
`scripts/build.sh` compiles objects with `gcc -c` and links with `ld` directly,
which sidesteps the problem entirely.

## Using the compiler

```
hphp file.hphp              run a HolyPHP program (default command)
hphp build file.hphp -o app compile to a native executable
hphp check file.hphp        type- and borrow-check only
hphp emit file.hphp         inspect the lowered internal representation
```

- One installed `hphp` binary is all you need; the runtime ships inside it.
- `run` builds in a private cache dir, executes, and cleans up — nothing lands in your project.
- `build -o app` produces a standalone executable you can distribute.
- `HPHP_VERBOSE=1` shows backend commands; `HPHP_KEEP_BIN=1` keeps `run`'s executable.

## Project layout

```
src/hphpc/
  lexer.c        tokenizer
  parser.c       recursive-descent parser → AST
  sema.c         name resolution, type inference, borrow checker
  builtins.c     PHP function signature table
  codegen.c      AST → C code generator
  main.c         driver (run / build / emit / check)
  runtime/       hphp_rt.c (value system) + hphp_std.c (PHP functions)
examples/        hello.hphp, demo.hphp (feature tour), websocket_demo.hphp (chat)
registry/        package registry server + pre-published packages
                 (websocket, ui, mathx) — see registry/README.md
tests/           positive/ (must run) + negative/ (must be rejected)
scripts/build.sh reliable build (gcc -c + direct ld)
```

## Testing

```bash
bash tests/positive/run_all.sh
```

Positive tests must compile **and** run to completion; negative tests must be
**rejected** by the type/borrow checker (immutable-borrow violations, type
mismatches, undefined names, raw-pointer use outside `unsafe`).

## Status & design notes

- Values are a tagged-union `hval` (null/int/float/bool/string/array/object/
  closure); strings and arrays are refcounted, arrays copy-on-write like PHP.
- Objects are refcounted smart pointers (`own<T>`/`Rc<T>`); `free()` is not part
  of the language.
- The borrow checker enforces aliasing XOR mutation on borrows at compile time.
- Closures capture by value into a heap environment struct; calls go through a
  runtime trampoline.
- User functions live in the `hpu_` namespace, so a function may be named `add`
  or `count` without colliding with a runtime helper.
- `try`/`catch`/`finally` compile to helper functions over an env struct of
  aliased locals; the program's only `setjmp` sits in the runtime
  (`hp_try_run`), Lisp/V8-style. That keeps generated code free of `setjmp`,
  which is what makes it safe under `-O2` — a `longjmp` across a frame with
  many register-allocated locals is undefined behaviour. A handler that throws
  runs under its own frame, so `finally` still runs before the rethrow.
- Expressions may span lines like PHP: newlines inside `( )` / `[ ]`, after an
  operator, or before a closing bracket are treated as whitespace; newlines
  inside `{ }` still separate statements.
- Arithmetic follows PHP: `.` concatenates, `+` on numeric strings adds
  (`"1" + "2"` is `3`), `/` always yields a float, and a map or array literal
  of mixed value types is `hval`-valued rather than mis-typed from its first
  entry.
- HolyPHP is a young language — see `tests/` for the exact feature set currently
  exercised (`controlflow.hphp` covers loops + exceptions, `values.hphp` literals,
  types and PHP arithmetic), and the examples for idiomatic usage.

## VS Code Extension

There is a dedicated editor extension for HolyPHP in `extension/hphp`
(highlighting, completions for all 180+ builtins, hover signatures, and live
`hphp check` diagnostics). Install the packaged VSIX:

```bash
code --install-extension extension/hphp/holyphp-0.1.0.vsix
```

See `extension/README.md` for details and development instructions
(`node scripts/genext.js` regenerates the builtin list from the compiler).
