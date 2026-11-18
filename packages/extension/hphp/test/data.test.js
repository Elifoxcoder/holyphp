/* test/data.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const data = require("../lib/data");

test("builtins.json is generated and complete", () => {
  const d = data.load();
  assert.ok(d.builtins.length >= 300, `only ${d.builtins.length} builtins`);
  const names = new Set();
  for (const b of d.builtins) {
    assert.ok(b.name && typeof b.name === "string", "builtin without name");
    assert.ok(!names.has(b.name), `duplicate builtin ${b.name}`);
    names.add(b.name);
    assert.ok(typeof b.sig === "string" && b.sig.includes("("), `${b.name} has no signature`);
    assert.ok(typeof b.desc === "string" && b.desc.length > 3, `${b.name} has no description`);
    assert.ok(typeof b.args === "number", `${b.name} has no arg count`);
    assert.equal(typeof b.unsafe, "boolean", `${b.name} unsafe is not boolean`);
    assert.ok(Array.isArray(b.params), `${b.name} params not parsed`);
  }
});

test("authored builtin reference data includes signatures and runnable examples", () => {
  const b = data.load().builtins;
  assert.ok(b.filter((x) => x.returns && x.params.length).length >= 280);
  assert.ok(b.filter((x) => x.example && x.exampleOutput !== undefined).length >= 300);
  const n = data.builtin("number_format");
  assert.equal(n.sig, "number_format(float|int $num[, int $decimals, string $dec_point, string $thousands_sep]): string");
  assert.equal(n.params[1].optional, true);
  assert.match(n.returns, /formatted number/);
});

test("parseSignature reads optional and variadic params", () => {
  const p = data.parseSignature("println(mixed $value[, int $colour]): void");
  assert.equal(p.length, 2);
  assert.deepEqual(p[0], { name: "$value", type: "mixed", optional: false, variadic: false });
  assert.deepEqual(p[1], { name: "$colour", type: "int", optional: true, variadic: false });

  const v = data.parseSignature("sprintf(string $format, ...$args): string");
  assert.equal(v.length, 2);
  assert.equal(v[1].variadic, true);
  assert.equal(v[1].name, "$args");

  assert.deepEqual(data.parseSignature("now(): int"), []);
});

test("returnType reads the part after the closing paren", () => {
  assert.equal(data.returnType("strlen(mixed $v): int"), "int");
  assert.equal(data.returnType("foo(): array<string>"), "array<string>");
  assert.equal(data.returnType("nonsense"), "");
});

test("constants mirror the compiler and carry values", () => {
  assert.equal(data.constant("RED").value, 2);
  assert.equal(data.constant("GREEN").value, 3);
  assert.equal(data.constant("RESET").value, 0);
  assert.equal(data.constant("PHP_INT_MAX").type, "int");
  assert.equal(data.constant("PHP_EOL").type, "string");
  assert.equal(data.constant("JSON_PRETTY_PRINT").value, 1);
  assert.equal(data.constant("NOPE"), null);
  assert.equal(data.isConstant("NOPE"), false);
});

test("no constant is advertised with a callable signature", () => {
  const d = data.load();
  for (const name of Object.keys(data.CONSTANTS)) {
    const b = d.byName.get(name);
    if (!b) continue;
    assert.equal(b.constant, true, `${name} not marked constant`);
  }
});

test("every builtin's params parse to named slots", () => {
  const d = data.load();
  for (const b of d.builtins) {
    for (const p of b.params) {
      assert.ok(/^\$/.test(p.name), `${b.name}: param without $: ${JSON.stringify(p)}`);
      assert.ok(p.type.length > 0, `${b.name}: param without type`);
    }
  }
});

test("keywords and types cover the language", () => {
  const words = new Set(data.KEYWORDS.map((k) => k[0]));
  for (const w of ["if", "foreach", "function", "class", "unsafe", "own", "mut", "let", "import", "global", "fn", "yield"]) {
    assert.ok(words.has(w), `missing keyword ${w}`);
  }
  for (const t of ["int", "u8", "string", "bool", "void", "Vec", "Option"]) {
    assert.ok(data.TYPES.includes(t), `missing type ${t}`);
  }
});
