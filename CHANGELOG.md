# Changelog

What changed for people who use HolyPHP. One section per released version,
newest first. Work that is not released yet goes under `## Unreleased`;
`scripts/ship.sh` renames that heading to the version it releases.

Write entries for users, not for the commit log: what they can do now, what
behaves differently, what they have to change.

## Unreleased

- Array-list and string reads outside their bounds now raise catchable errors instead of returning an empty value or risking an invalid access. Missing map keys still return `null`.
- Files can use `declare(enforce_memory_safety=1);` to make unsafe blocks and calls to builtins marked unsafe compile errors.
- Thread checking currently accepts only direct literal callbacks with immutable scalar/string captures; indirect callbacks and class captures without a verified `Sync` contract are rejected. Channel ownership transfer is not yet enforced, so this is not a complete cross-thread safety guarantee.
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
