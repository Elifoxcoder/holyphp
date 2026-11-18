/* test/symbols.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { collectSymbols, enclosingClass, variableType, parseParams } = require("../lib/symbols");

const SRC = `<?php
import "lib/ui";

const VERSION = "1.0";

function add(int $a, int $b = 0): int {
    return $a + $b;
}

class User {
    public string $name;
    private int $age;

    public function __construct(string $name) {
        $this->name = $name;
        $this->age = 0;
    }

    public function greet(string $who): string {
        return "hi " + $who;
    }
}

interface Shape {
    public function area(): float;
}

enum Color {
    Red,
    Green
}

let $u: User = new User("x");
$count = add(1, 2);
`;

test("functions are top-level only", () => {
  const s = collectSymbols(SRC);
  assert.deepEqual(s.functions.map((f) => f.name), ["add"]);
  const add = s.functions[0];
  assert.equal(add.ret, "int");
  assert.deepEqual(add.params.map((p) => [p.name, p.type, p.optional]), [
    ["$a", "int", false],
    ["$b", "int", true],
  ]);
});

test("declarations expose exact name offsets for editor navigation", () => {
  const s = collectSymbols(SRC);
  for (const item of [s.classes.find((x) => x.name === "User"), s.functions.find((x) => x.name === "add"), s.constants.find((x) => x.name === "VERSION")]) {
    assert.equal(SRC.slice(item.nameOffset, item.nameOffset + item.name.length), item.name);
  }
  const user = s.classes.find((x) => x.name === "User");
  for (const item of [...user.methods, ...user.fields]) assert.equal(SRC.slice(item.nameOffset, item.nameOffset + item.name.length), item.name);
});

test("classes carry methods, fields and constants", () => {
  const s = collectSymbols(SRC);
  const names = s.classes.map((c) => c.name);
  assert.deepEqual(names, ["User", "Shape", "Color"]);

  const user = s.classes.find((c) => c.name === "User");
  assert.deepEqual(user.methods.map((m) => m.name), ["__construct", "greet"]);
  assert.deepEqual(user.fields.map((f) => [f.name, f.type]), [
    ["name", "string"],
    ["age", "int"],
  ]);
  assert.equal(user.methods[1].ret, "string");
  assert.equal(user.methods[1].params[0].name, "$who");
});

test("imports and top-level constants", () => {
  const s = collectSymbols(SRC);
  assert.deepEqual(s.imports.map((i) => i.name), ["lib/ui"]);
  assert.deepEqual(s.constants.map((c) => c.name), ["VERSION"]);
});

test("variables come from let, assignment and parameters", () => {
  const s = collectSymbols(SRC);
  const names = s.variables.map((v) => v.name);
  for (const n of ["u", "count", "a", "b", "name", "age", "who"]) {
    assert.ok(names.includes(n), `missing variable $${n}`);
  }
  const u = s.variables.find((v) => v.name === "u");
  assert.equal(u.type, "User");
  assert.equal(u.declared, true);
});

test("declarations inside comments and strings are ignored", () => {
  const s = collectSymbols(`
// function fake(): int { return 1; }
let $s = "class Hidden { }";
function real(): void { }
`);
  assert.deepEqual(s.functions.map((f) => f.name), ["real"]);
  assert.deepEqual(s.classes.map((c) => c.name), []);
});

test("enclosingClass finds the innermost class", () => {
  const s = collectSymbols(SRC);
  const idx = SRC.indexOf("$this->age");
  const c = enclosingClass(s, idx);
  assert.ok(c);
  assert.equal(c.name, "User");
  assert.equal(enclosingClass(s, 0), null);
});

test("variableType reads let, new and parameter types", () => {
  const s = collectSymbols(SRC);
  assert.equal(variableType(s, SRC, "u"), "User");
  const text2 = "function f(Point $p): void { $q = new Point(); }";
  const s2 = collectSymbols(text2);
  assert.equal(variableType(s2, text2, "p"), "Point");
  assert.equal(variableType(s2, text2, "q"), "Point");
  assert.equal(variableType(s2, text2, "nope"), "");
});

test("parseParams handles defaults and generics", () => {
  assert.deepEqual(
    parseParams('int $a, Map<string, int> $m, string $s = "x"').map((p) => p.name),
    ["$a", "$m", "$s"]
  );
  assert.equal(parseParams("Map<string, int> $m")[0].type, "Map<string, int>");
});
