/* test/text.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const t = require("../lib/text");

test("lineStarts / positionAt / offsetAt round-trip", () => {
  const text = "abc\ndef\n\nghi";
  const starts = t.lineStarts(text);
  assert.deepEqual(starts, [0, 4, 8, 9]);
  assert.deepEqual(t.positionAt(text, 0), { line: 0, character: 0 });
  assert.deepEqual(t.positionAt(text, 5), { line: 1, character: 1 });
  assert.deepEqual(t.positionAt(text, 9), { line: 3, character: 0 });
  assert.equal(t.offsetAt(text, 1, 2), 6);
  assert.equal(t.offsetAt(text, 9, 0), 9);           // line clamped to the last one
  assert.equal(t.offsetAt(text, 1, 999), 7);         // clamped to line end
});

test("lineText strips the newline", () => {
  const text = "a\r\nb\nc";
  assert.equal(t.lineText(text, 0), "a");
  assert.equal(t.lineText(text, 1), "b");
  assert.equal(t.lineText(text, 2), "c");
});

test("wordRangeAt includes $ and digits", () => {
  const text = "  $foo_bar1 = 2;";
  const r = t.wordRangeAt(text, 5);
  assert.equal(r.word, "$foo_bar1");
  assert.equal(r.start, 2);
  assert.equal(r.end, 11);
});

test("linePrefix returns the text before the cursor", () => {
  const text = "one\ntwo three";
  assert.equal(t.linePrefix(text, 6), "tw");
  assert.equal(t.linePrefix(text, 13), "two three");
});

test("stripNonCode blanks comments and strings but keeps offsets", () => {
  const src = [
    'let $s = "function fake() {";',
    "// function commented() {",
    "/* class Hidden { } */",
    "function real(): int { return 1; }",
  ].join("\n");
  const out = t.stripNonCode(src);
  assert.equal(out.length, src.length);
  assert.ok(!/fake/.test(out));
  assert.ok(!/commented/.test(out));
  assert.ok(!/Hidden/.test(out));
  assert.ok(/function real/.test(out));
  assert.equal(out.split("\n").length, src.split("\n").length);
});

test("stripNonCode handles # comments but not #[", () => {
  const src = "# let $x = 1;\n#[Attr]\nlet $y = 2;";
  const out = t.stripNonCode(src);
  assert.ok(!/\$x/.test(out));
  assert.ok(/Attr/.test(out));
  assert.ok(/\$y/.test(out));
});

test("countTopLevelCommas ignores nested calls and strings", () => {
  assert.equal(t.countTopLevelCommas('f(a, "x,y", g(1, 2))', 2, 20), 2);
});

test("findOpenParen finds the innermost unclosed paren", () => {
  const src = "f(g(1, 2), h(";
  const i = t.findOpenParen(src, src.length);
  assert.equal(src[i], "(");
  assert.equal(src.slice(0, i).trim().endsWith("h"), true);
});

test("findOpenParen ignores parens inside strings", () => {
  const src = 'f("(", 1';
  const i = t.findOpenParen(src, src.length);
  assert.equal(i, 1);
});
