/* test/integration.test.js — the real compiler, end to end.
 *
 * Skipped (not failed) when no `hphp` is on PATH: the pure tests above still
 * cover the parsing, and CI without a compiler should stay green.
 */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { fixture, hphpAvailable, check } = require("./helpers");
const { parseCompilerOutput } = require("../lib/diagnostics");

const has = hphpAvailable();
const skip = has ? false : "hphp not on PATH (set HPHP=/path/to/hphp)";

test("a valid program produces no diagnostics", { skip }, () => {
  const f = fixture(`<?php
function add(int $a, int $b): int {
    return $a + $b;
}
echo add(1, 2);
`);
  const r = check(f);
  assert.equal(r.status, 0, r.output);
  assert.deepEqual(parseCompilerOutput(r.output), []);
});

test("a syntax error is reported with a line and column", { skip }, () => {
  const f = fixture(`<?php
function f(int $x): int {
    return $x +
}
`);
  const r = check(f);
  assert.notEqual(r.status, 0);
  const diags = parseCompilerOutput(r.output);
  assert.ok(diags.length >= 1, "no diagnostic parsed from:\n" + r.output);
  const e = diags[0];
  assert.equal(e.severity, "error");
  assert.ok(e.line >= 1, "no line");
  assert.ok(e.message.length > 0, "empty message");
});

test("a type error is reported", { skip }, () => {
  const f = fixture(`<?php
let $n: int = 1;
$n = "text";
`);
  const r = check(f);
  assert.notEqual(r.status, 0);
  const diags = parseCompilerOutput(r.output);
  assert.ok(diags.some((d) => /cannot assign|type/i.test(d.message)), r.output);
});

test("errors point at the right file", { skip }, () => {
  const f = fixture(`<?php
let $n: int = 1;
$n = "text";
`, "wrong.hphp");
  const diags = parseCompilerOutput(check(f).output);
  assert.ok(diags.length > 0);
  for (const d of diags) {
    assert.ok(d.file === null || /wrong\.hphp$/.test(d.file.replace(/\\/g, "/")), `unexpected file ${d.file}`);
  }
});

test("a UTF-8 BOM is not an error", { skip }, () => {
  const f = fixture("\uFEFF<?php\nfunction f(): int { return 1; }\n", "bom.hphp");
  const r = check(f);
  assert.equal(r.status, 0, r.output);
  assert.deepEqual(parseCompilerOutput(r.output), []);
});

test("line 1 columns are 1-based like every other line", { skip }, () => {
  const f = fixture("<?php $x = ;\n", "cols.hphp");
  const diags = parseCompilerOutput(check(f).output);
  assert.ok(diags.length > 0, "expected a syntax error");
  assert.equal(diags[0].line, 1);
  assert.ok(diags[0].col >= 1, `line 1 reported column ${diags[0].col}`);
});
