# HolyPHP memory model

This document specifies the ownership contract for phase 1 and the later region
optimization. HolyPHP already checks a subset of borrows. Generated ownership
cleanup and runtime reclamation are implemented for the paths listed below.
Automatic cycle collection covers object/closure graphs, and weak properties
are supported. Fresh owned operands in binary expressions are released after
use, including managed builtin results that do not alias an input. The wider
borrowing-consumer audit and complete builtin ownership table remain
incomplete.

## Values and owners

Scalars (`int`, `float`, `bool`, enum values and `null`) are copied directly.
A `string` is an immutable byte string backed by `hstr`; arrays (`array`, `vec`,
`map`, `set`) use `harr` and value semantics; closures use `hclosure`; class
instances use a generated structure. These four heap kinds are reference-counted
handles. A handle copy retains one owner, and leaving an owner releases it.
Strings are immutable. Arrays share storage until a write, then detach if shared.
Objects and closures have identity and are shared by handle.

An initialized variable slot owns its value. Reassignment evaluates the new
value first, retains it if it is borrowed, installs it, then releases the old
value. A move transfers the owner and clears its source. A function parameter
receives an owner for a by-value heap argument; a return transfers an owner to
the caller. Borrowed arguments do not change the count. Temporary expression
owners are released at the end of their full expression. Containers own each
stored key and value. A removed or replaced entry is released; COW cloning
retains every copied key/value. A closure owns each by-value capture and holds
its by-reference captures through a shared box. Dropping a closure releases
those captures and boxes.

Every control-flow exit must run the same drops: normal scope exit, `return`,
`break`/`continue` across a scope, and exception propagation. Catch bindings own
the exception value for the catch body's lifetime. The exception slot owns a
thrown value until it is handled or propagated. Generated cleanup is lexical
and path-specific; it must not depend on C stack unwinding or `longjmp` crossing
live owners.

## Copy-on-write

Plain array assignment shares the backing `harr`. A read is borrowed. Before a
write, the compiler/runtime obtains a writable owner: if the count is one, it
writes in place; otherwise it clones the ordered entries and retains their
heap values, releases the caller's old owner, and writes to the clone. Mutating
builtins follow the same contract. String writes always produce a distinct
string value unless the compiler proves unique ownership and the operation is
an in-place append to that same variable. `&mut` excludes aliases that would
invalidate this proof.

## Borrows and explicit ownership types

`&$x` is a read-only borrow whose lifetime is checked against its owner. It is
not a new reference-count owner. `&mut $x` is an exclusive borrow; while active,
no other read/write alias may be used. A by-reference PHP-style parameter
(`T &$x`) remains an alias to the caller's slot, as documented by the language
reference; it is distinct from a Rust-style borrow expression.

`own<T>` means unique ownership. Assignment moves it; use after move is a
compile error. `Rc<T>` means shared strong ownership and maps to the same
retain/release mechanism as a heap handle. `Weak<T>` is a non-owning object
handle; upgrading yields an optional strong owner and fails after destruction.
Plain HolyPHP code needs none of these annotations.

## Cycles and weak properties

Reference counting alone cannot reclaim strong cycles. The default object
policy therefore includes a cycle collector; there is no global opt-out.
Objects, arrays, and closures participate in a runtime graph. Generated class
and closure trace callbacks expose strong fields and captures; arrays are
traced by the runtime. At safe points the collector subtracts internal
incoming edges from trial counts,
marks nodes with external owners as roots, then frees unmarked groups after
invalidating weak handles. Small heaps are scanned on release; larger heaps are
scanned after batches of reference releases. A pass is O(V+E) over registered
nodes and edges. Before a program starts a worker thread, RC updates use the
single-thread path and collector operations do not take the collector mutex.
Thread creation switches the process to synchronized reference counts and a
locked collector registry. Collection must not run while an object graph is
being mutated or while a borrowed reference is exposed to native code.

`public weak Node $parent;` stores a weak handle. Reading it returns `Node?`;
if the target has died it returns `null`. Assigning a target does not keep it
alive. Clearing/replacing the property unregisters its weak edge. Weak handles
are invalidated before object storage is reclaimed.

## Builtin ownership contract (for task 1.4)

Each builtin parameter and result should be classified in the builtin table or a
mechanically checked sidecar as one of:

- **borrow**: input is read during the call only; no retain is stored;
- **retain**: input is retained if the implementation stores it;
- **consume**: the caller transfers one owner, including on error paths;
- **new**: result returns one owned value;
- **alias**: result borrows/aliases an input and must be retained before it
  escapes the call.

The default for ordinary HolyPHP builtins is borrowed inputs and an owned
result. Any builtin that stores an argument, returns an alias, or consumes an
owner must declare that exception. The checker should reject an unclassified
heap parameter/result. Error exits follow the same ownership rule as success.

## Threads and regions

Reference counts are not permission to share mutable state between threads.
A value may cross a thread boundary only when it is immutable/`Sync`, or through
a channel that transfers ownership. Mutable arrays and ordinary objects are not
`Sync`; the borrow checker must reject shared mutable aliases. Atomic reference
counts alone do not make object fields thread-safe.

The first Phase 2 escape-analysis slice is available in
`declare(enforce_memory_safety=1);` files. For free functions, a heap parameter
used only by `count()`, `array_sum()`, `strlen()`, or forwarded to another
proven borrowing parameter is passed without the call-boundary retain, and the
callee does not register a parameter drop. The analysis is conservative:
returns, stores, writes, closures, methods, unknown calls, and function values
keep the ordinary reference-counted path.

A local binding outside loops, initialized from a scalar-only array literal,
can use a function-local arena when all of its uses meet the same read-only
proof. This includes bindings in nested blocks, conditional branches, and
switch arms. Try/catch bodies are not included because they use separate
generated functions. The arena is released through the normal cleanup path,
including early returns.
Canonical counted loops can also omit an index bounds check when the array and
induction variable remain stable; in safety mode, the body may pass that array
only to a proven borrower. Safety mode also puts eligible scalar-array and
string locals in reusable loop-iteration regions when their uses stay within
the loop and the existing conservative escape scan proves non-escape. The
region resets at each iteration and is released on loop exit. Method bodies
also participate in local-region inference. For calls on a concrete class,
parameter borrowing is accepted only when every implementation reachable by
virtual dispatch proves that parameter read-only and nonescaping. Interface
or unknown receiver dispatch stays on ordinary RC. Receiver summaries do not
yet establish receiver non-escape. General escape promotion and broad
removal of retain/release and bounds checks are not implemented yet. Values that do not
match a proof continue to use the existing RC/COW representation. Both safety
and compatibility modes use the same value representation, so this remains an
internal allocation and ownership optimization; `own<T>`, `Rc<T>`, borrows,
and ordinary code keep their meaning.

Unsafe raw addresses are represented as integers and do not participate in
managed-object reference counting. `malloc()` results can be passed to
`memset()`, `memcpy()`, `free()`, and byte accessors without scope cleanup
interpreting arbitrary memory as an object header. `memset()` and `memcpy()`
return the same address they receive. `stream_recv()` returns an owned string
copy even when the Linux io_uring backend reads into a reusable ring buffer.
Builtin temporary release also respects borrowed array elements and aliases.
Literal-pool strings are immortal; internal cleanup leaves them alone.

## Test plan

1. Runtime unit tests for retain/release of strings, nested arrays, closures,
   object fields, replacement, and moved values.
2. Compiler tests for assignment, parameters, returns, shadowed locals, early
   returns, loop exits, closure captures, and exception/catch/finally paths.
3. COW tests prove mutations preserve aliases, including nested arrays and
   maps; uniqueness tests cover the in-place path.
4. Long loops repeatedly allocate and drop strings/arrays/closures and assert
   live heap bytes return to a stable baseline. Run all tests under ASan,
   UBSan and LeakSanitizer.
5. Cycle tests cover self/mutual cycles, weak upgrade before/after collection,
   and ensure acyclic object workloads do not trigger a full-heap scan each
   release.
6. Thread tests reject shared mutable values and accept immutable `Sync` values
   and channel ownership transfer.

## Decisions taken

- The collector is automatic for object cycles; `weak` is a property modifier,
  not a global mode. This follows plan section 7.
- Collection is deferred to safe points and candidate subgraphs, so the common
  object release does not scan the heap.
- Array cycles are not part of the language's safe construction model; cycles
  are handled through identity-bearing objects. If array references can form
  cycles through dynamic aliases, they must be added to the collector graph
  before that feature is accepted.
- Builtin ownership defaults to borrowed inputs and owned results; exceptional
  aliasing/storage/consumption is explicit.

## Current implementation boundary

Generated string literal sites cache their immortal runtime handle after the
first evaluation. Owner cleanup uses a reusable per-thread vector; registration
appends a slot and scope marks are vector indices. In safety mode, the compiler
now infers conservative borrow-only summaries for free-function parameters
used only by `count`, `array_sum`, `strlen`, or calls to another proven
borrow-only parameter. A direct local variable passed to such a parameter does
not get a retain/release pair at the call boundary. Returns, stores, writes,
closures, unknown calls, interface-dispatched methods, and function values
keep the ordinary RC path. A method argument is borrowed only when all
concrete implementations for the receiver's class dispatch prove read-only
non-escape. A top-level
function local initialized from a scalar-only array literal
also uses a function-local arena when every use is a read-only builtin or
proven borrowing call. These arrays have no RC or collector registration and
the arena is released on every function exit. Escaping arrays keep the normal
RC/COW path. The same summaries allow a canonical counted loop in safety mode
to omit index checks when its body passes the iterated array only to a proven
borrower. Method bodies also use local-region inference. Calls on concrete
class receivers can borrow a direct local argument only when every reachable
override proves the parameter read-only and nonescaping; interface and unknown
dispatch remain reference-counted. Eligible loop-local scalar arrays and
strings use a reusable per-loop arena. Method receiver summaries do not yet
establish receiver non-escape, and broader whole-file
escape analysis is still not implemented; other indexed reads keep their
runtime checks.

The runtime implements reference counts and drop paths for strings, arrays,
closures, and generated class objects. Generated code retains borrowed heap
values on assignment and call boundaries, releases locals on scope exit and
nonlocal exits, drops closure captures, and handles exception cleanup. Array
writes, including nested indexed writes, detach shared arrays. A churn test
checks that RSS stabilizes for strings, arrays, objects, and capturing closures.

Generated class and closure callbacks trace strong references. The collector
reclaims cycles and invalidates weak properties before freeing object storage.
The RSS regression includes self-referential objects and object/closure cycles.
Builtin ownership metadata also remains for task 1.4, so builtins that retain,
consume, or alias heap arguments still require a separate audit. WSL is
unavailable in the current environment, so sanitizer verification has not run.

The generated `hp_main()` function now uses the same cleanup stack as ordinary
functions: heap locals declared by top-level script code are registered and
dropped when the script finishes. File-scope values retained for `global`
bindings remain reachable through their static slots until process exit.
`putenv()` copies its `NAME=VALUE` input into the operating system environment,
so the managed input string can safely leave scope after the call. Fresh
strings inserted into arrays by `explode()`, `preg_split()`, `scandir()`, and
`pathinfo()` transfer their initial owner to the array rather than retaining a
second temporary owner.

Signed integer `+`, `-`, `*`, unary negation, left shifts and integer power use
unsigned arithmetic for defined two's-complement wrapping in the generated
64-bit and 128-bit paths. Converting an unsigned result back to the signed
representation is implementation-defined in C, but does not invoke signed
overflow undefined behavior on the supported GCC targets. The minimum signed
value divided by `-1` wraps to itself, and its remainder is zero. Shift counts
outside the operand width produce zero for left shift and sign-fill for
arithmetic right shift.

Array-list and string indexing checks bounds in the runtime and throws a
catchable error on an invalid read. Map lookups remain nullable for missing
keys. A narrow compiler proof removes the per-read check for
`for ($i = 0; $i < count($a); $i++)` when the body contains no calls, writes to
`$a`, writes to `$i`, or unsupported control effects. All other indexed reads
keep runtime checks. `foreach` advances a checked runtime iterator directly
and does not perform an indexed lookup for each element.

The parser and semantic checker support the per-file
`declare(enforce_memory_safety=1);` directive. It rejects unsafe blocks and
builtin calls whose `builtins.c` entry has its unsafe flag set. Safe code
continues to compile normally. The borrow checker analyzes the file's script
and functions, but proven ownership is not yet used to remove reference-count
operations or bounds checks. The directive does not imply that all of the
remaining guarantees in the safety plan have been implemented.

Thread callbacks are checked at the point where a known closure crosses the
thread boundary. A closure capture or channel payload moves from its source;
the borrow checker rejects later uses of that source. Immutable scalar and
string values are shareable. Arrays and maps can move when their nested values
are themselves safe value types; mutation then uses copy-on-write. Ordinary
class objects and closures cannot be sent as channel payloads. Unknown
`mixed` channel payloads are checked again by the runtime. Heap reference
counts and the collector registry use synchronization so independent worker
threads can retain and drop immutable shared values safely; this does not make
mutable object fields `Sync`.

The compiler accepts an indirect callback only when its static type carries a
closure origin or a `thread_callable` contract established at a checked call
boundary. The bundled `Thread`, `Pool.map`, channel, and chunked-map wrappers
use that contract. The runtime moves the closure environment into the worker;
each invocation creates owned local captures so concurrent calls detach
copy-on-write arrays independently.
