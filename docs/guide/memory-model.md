# HolyPHP memory model

This document specifies the ownership contract for phase 1 and the later region
optimization. HolyPHP already checks a subset of borrows. Generated ownership
cleanup and runtime reclamation are implemented for the paths listed below;
cycle collection, weak properties, and builtin ownership metadata remain later
work.

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
Objects participate in a runtime graph, with strong object fields counted as
edges and weak properties excluded. At safe points (allocation pressure and
periodically after a batch of object releases), the collector identifies
candidate objects, computes their internal incoming strong edges, and treats
objects with external owners as roots. Unreachable candidates are marked as a
group, their fields are dropped once, weak handles are cleared, then storage is
freed. The common acyclic path costs only retain/release operations; a cycle
pass costs O(V+E) over the candidate subgraph and is amortized. Collection must
not run while an object graph is being mutated or while a borrowed reference is
exposed to native code.

`public weak Node $parent;` stores a weak handle. Reading it returns `Node?`;
if the target has died it returns `null`. Assigning a target does not keep it
alive. Clearing/replacing the property unregisters its weak edge. Weak handles
are invalidated before object storage is reclaimed.

## Builtin ownership contract (for task 1.4)

Each builtin parameter and result must be classified in the builtin table or a
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

Phase 2 adds escape analysis. A value proven not to escape its function/request
may be allocated in a region. Region references use the same source-level
ownership rules, but retain/release within the region can become no-ops; region
exit reclaims all contained allocations together. Escaping values are promoted
or copied to reference-counted storage before the region closes. The region is
an allocation strategy hidden from users, so `own<T>`, `Rc<T>`, borrows and
ordinary code keep their meaning.

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

The runtime implements reference counts and drop paths for strings, arrays,
closures, and generated class objects. Generated code retains borrowed heap
values on assignment and call boundaries, releases locals on scope exit and
nonlocal exits, drops closure captures, and handles exception cleanup. Array
writes, including nested indexed writes, detach shared arrays. A churn test
checks that RSS stabilizes for strings, arrays, objects, and capturing closures.

The current generated object header has a class-specific drop callback, but
does not yet trace strong object edges. Consequently strong object cycles can
remain allocated; weak-property syntax and invalidation are not implemented.
Builtin ownership metadata also remains for task 1.4, so builtins that retain,
consume, or alias heap arguments still require a separate audit. WSL is
unavailable in the current environment, so sanitizer verification has not run.
