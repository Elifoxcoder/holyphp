/* test/diagnostics.test.js — the compiler output formats that matter. */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const d = require("../lib/diagnostics");

test("sema error with column", () => {
  const [e] = d.parseCompilerOutput("a.hphp:12:5: error: cannot assign string to int");
  assert.deepEqual(e, {
    file: "a.hphp", line: 12, col: 5, severity: "error",
    message: "cannot assign string to int",
  });
});

test("error without a column", () => {
  const [e] = d.parseCompilerOutput("a.hphp:3: warning: unused variable");
  assert.equal(e.line, 3);
  assert.equal(e.col, 0);
  assert.equal(e.severity, "warning");
});

test("fatal syntax error (hphpc: error: file:line:col: msg)", () => {
  const [e] = d.parseCompilerOutput(
    "hphpc: error: synerr.hphp:4:1: unexpected token '}' in expression"
  );
  assert.equal(e.file, "synerr.hphp");
  assert.equal(e.line, 4);
  assert.equal(e.col, 1);
  assert.equal(e.severity, "error");
  assert.equal(e.message, "unexpected token '}' in expression");
});

test("fatal error without a location", () => {
  const [e] = d.parseCompilerOutput("hphpc: error: cannot open 'nope.hphp'");
  assert.equal(e.file, null);
  assert.equal(e.message, "cannot open 'nope.hphp'");
});

test("noise lines are ignored", () => {
  const out = [
    "a.hphp: OK (types and borrows check)",
    "hphp: compilation failed",
    "",
  ].join("\n");
  assert.deepEqual(d.parseCompilerOutput(out), []);
});

test("multiple errors and CRLF", () => {
  const out = [
    "a.hphp:4:5: error: cannot assign to $n while borrowed",
    "a.hphp:5:5: error: return type mismatch: expected int, got &int",
    "hphp: compilation failed",
  ].join("\r\n");
  const list = d.parseCompilerOutput(out);
  assert.equal(list.length, 2);
  assert.equal(list[1].message, "return type mismatch: expected int, got &int");
});

test("Windows paths keep their drive letter", () => {
  const [e] = d.parseCompilerOutput("C:\\proj\\sub\\a.hphp:4:5: error: bad thing");
  assert.equal(e.file, "C:\\proj\\sub\\a.hphp");
  assert.equal(e.line, 4);
  assert.equal(e.col, 5);
});

test("a message may contain colons", () => {
  const [e] = d.parseCompilerOutput("a.hphp:1:2: error: expected ':' after key");
  assert.equal(e.message, "expected ':' after key");
});

test("samePath is case-insensitive on Windows", () => {
  assert.equal(d.samePath("C:\\x\\a.hphp", "c:\\X\\A.hphp"), process.platform === "win32");
  assert.equal(d.samePath("a.hphp", "a.hphp"), true);
  assert.equal(d.samePath("a.hphp", "b.hphp"), false);
});

test("rangeFor points at the quoted token", () => {
  const lines = ["function f() {", "    return 1 +", "}"];
  const r = d.rangeFor(lines, 3, 1, "unexpected token '}' in expression");
  assert.deepEqual(r, { line: 2, start: 0, end: 1 });
});

test("rangeFor extends over an identifier", () => {
  const lines = ["let $value = 1;"];
  const r = d.rangeFor(lines, 1, 5, "cannot assign string to int");
  assert.deepEqual(r, { line: 0, start: 4, end: 10 });
});

test("rangeFor without a column covers the line", () => {
  const lines = ["hello world"];
  const r = d.rangeFor(lines, 1, 0, "something");
  assert.deepEqual(r, { line: 0, start: 0, end: 11 });
});

test("rangeFor clamps beyond the end of the document", () => {
  const lines = ["a", "b"];
  const r = d.rangeFor(lines, 99, 99, "x");
  assert.equal(r.line, 1);
  assert.ok(r.end <= 1);
});

test("rangeFor covers a string literal", () => {
  const lines = ['echo "unterminated'];
  const r = d.rangeFor(lines, 1, 6, "unterminated string literal");
  assert.equal(r.start, 5);
  assert.equal(r.end, lines[0].length);
});

test("quotedToken takes the first quoted token", () => {
  assert.equal(d.quotedToken("unexpected token '}' in expression"), "}");
  assert.equal(d.quotedToken("no quotes here"), "");
});
