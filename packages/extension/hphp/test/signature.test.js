/* test/signature.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { signatureAt } = require("../lib/signature");

function at(text, marker) {
  const i = text.lastIndexOf(marker) + marker.length;
  return signatureAt(text, i);
}

test("simple call, first argument", () => {
  const s = at("strlen(", "");
  assert.equal(s.name, "strlen");
  assert.equal(s.active, 0);
});

test("second argument", () => {
  const s = at('println("hi", ', "");
  assert.equal(s.name, "println");
  assert.equal(s.active, 1);
});

test("nested call counts only the inner commas", () => {
  const s = at("f(g(1, 2), ", "");
  assert.equal(s.name, "f");
  assert.equal(s.active, 1);
});

test("string commas do not count", () => {
  const s = at('f("a,b", ', "");
  assert.equal(s.active, 1);
});

test("control keywords are not calls", () => {
  assert.equal(at("if (", ""), null);
  assert.equal(at("foreach (", ""), null);
});

test("outside any call", () => {
  assert.equal(signatureAt("let $x = 1;", 11), null);
});

test("closed calls are not reported", () => {
  assert.equal(signatureAt("f(1, 2) + ", 10), null);
});
