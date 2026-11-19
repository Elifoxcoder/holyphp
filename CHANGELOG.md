# Changelog

What changed for people who use HolyPHP. One section per released version,
newest first. Work that is not released yet goes under `## Unreleased`;
`scripts/ship.sh` renames that heading to the version it releases.

Write entries for users, not for the commit log: what they can do now, what
behaves differently, what they have to change.

## Unreleased

- The semantic checker now recovers with an error type when an invalid or partially parsed expression has no type, avoiding a compiler crash. `array_sum()` promotes to a float when summing integers would overflow.
- Array-list and string reads outside their bounds now raise catchable errors instead of returning an empty value or risking an invalid access. Missing map keys still return `null`.
- Canonical counted loops over a stable array avoid repeated index bounds checks when the compiler can prove the index stays in range. Other indexed reads keep their checks.
- Files can use `declare(enforce_memory_safety=1);` to make unsafe blocks and calls to builtins marked unsafe compile errors.
- Thread captures and channel payloads now move their source values; later use of a moved source is rejected. Immutable scalar and string captures can be shared, and the bundled `Thread` and `Pool` wrappers use statically checked `thread_callable` callbacks.
- Strings, arrays, closures, and class instances now release generated owners automatically; shared arrays detach on write. Object, array, and closure cycles are collected automatically, and `weak` class properties clear when their targets die.
- Ordinary `new Class()` expressions now have the normal copyable class type; explicit `own Class` slots can still take a fresh instance.
- By-reference calls to mutating builtins are checked as one write, and separate scalar by-reference arguments no longer conflict merely because their storage is boxed.
- Closure escape diagnostics now distinguish values actually passed to a retaining callback from unrelated heap values, while keeping conservative borrow checks for unknown callbacks.
- The runtime now reports the same version as the compiler (`phpversion()`
  said 1.0.0 while `hphp --version` said 1.0.2).

## 1.0.2

Recorded after the fact; this is the last version before the changelog was
introduced.

- UI library 2.0: one theme, no coordinates, text measured in logical pixels,
  scrolling windows.
- Chromium (WebView2) can be hosted in a HolyPHP window.
- Threading and async libraries; imports without a file extension.
- Release pipeline fixes: stale installer payload, registry URL, version skew;
  winget manifests at spec 1.12.0.
