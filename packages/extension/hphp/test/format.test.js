"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const { formatIndentation } = require("../lib/format");

test("indentation formatter handles nested blocks and closing braces", () => {
  const input = "class A {\nfunction f() {\nreturn 1;\n}\n}\n";
  assert.equal(formatIndentation(input, 2, true), "class A {\n  function f() {\n    return 1;\n  }\n}\n");
});

test("indentation formatter ignores braces in strings and comments", () => {
  const input = 'function f() {\n echo "}"; // {\n}\n';
  assert.equal(formatIndentation(input, 2, true), 'function f() {\n  echo "}"; // {\n}\n');
});
