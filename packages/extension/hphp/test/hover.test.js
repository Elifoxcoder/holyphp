/* test/hover.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { hoverFor } = require("../lib/hover");
const { collectSymbols } = require("../lib/symbols");

const SRC = `<?php
const LIMIT = 10;
function add(int $a, int $b): int { return $a + $b; }
class Point {
    public int $x;
    public function len(): float { return 0.0; }
}
let $p: Point = new Point();
$p->len();
`;

function hoverAt(text, marker, delta) {
  const s = collectSymbols(text);
  const offset = text.indexOf(marker) + (delta === undefined ? 0 : delta);
  return hoverFor(text, offset, s);
}

test("builtin hover shows the signature and description", () => {
  const h = hoverAt("strlen($s);", "strlen");
  assert.ok(h);
  assert.match(h.markdown, /builtin/);
  assert.match(h.markdown, /strlen\(string\|array \$v\): int/);
  assert.match(h.markdown, /bytes/);
});

test("constant hover shows the value", () => {
  const h = hoverAt("println($x, GREEN);", "GREEN");
  assert.ok(h);
  assert.match(h.markdown, /predefined constant/);
  assert.match(h.markdown, /GREEN = 3/);
});

test("variable hover shows its declared type", () => {
  const h = hoverAt(SRC, "$p", 1);
  assert.ok(h);
  assert.match(h.markdown, /\$p/);
  assert.match(h.markdown, /Point/);
});

test("user function hover", () => {
  const h = hoverAt(SRC, "add(", 1);
  assert.ok(h);
  assert.match(h.markdown, /function add\(int \$a, int \$b\): int/);
});

test("class hover lists members", () => {
  const h = hoverAt(SRC, "Point {", 1);
  assert.ok(h);
  assert.match(h.markdown, /class Point/);
  assert.match(h.markdown, /function len\(\): float/);
  assert.match(h.markdown, /int \$x/);
});

test("keyword hover explains the syntax", () => {
  const h = hoverAt("foreach ($a as $v) {}", "foreach");
  assert.ok(h);
  assert.match(h.markdown, /foreach/);
  assert.match(h.markdown, /as \$k => \$v/);
});

test("type hover", () => {
  const h = hoverAt("let $x: Vec = [];", "Vec");
  assert.ok(h);
  assert.match(h.markdown, /type/);
});

test("nothing to say about plain words", () => {
  assert.equal(hoverAt("some random text", "random"), null);
});
