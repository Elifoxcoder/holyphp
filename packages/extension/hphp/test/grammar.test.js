/* test/grammar.test.js — the TextMate grammar must cover the real language. */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("fs");
const path = require("path");

const grammarPath = path.join(__dirname, "..", "syntaxes", "hphp.tmLanguage.json");
const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf8"));

test("grammar has the expected top-level shape", () => {
  assert.equal(grammar.scopeName, "source.hphp");
  assert.ok(Array.isArray(grammar.patterns) && grammar.patterns.length > 0);
  assert.ok(grammar.repository && typeof grammar.repository === "object");
});

test("every repository rule has a match or a begin/end pair", () => {
  for (const [name, rule] of Object.entries(grammar.repository)) {
    const patterns = rule.patterns || [rule];
    for (const p of patterns) {
      const ok = (p.match && p.name) || (p.begin && p.end && p.name) || p.include;
      assert.ok(ok, `rule ${name} has no match/begin/end: ${JSON.stringify(p)}`);
    }
  }
});

test("all compiler builtins are highlighted, not a hand-written subset", () => {
  const patterns = grammar.repository.functions.patterns;
  const builtin = patterns.find((p) => p.name === "support.function.builtin.hphp");
  assert.ok(builtin, "no builtin pattern");
  for (const name of ["strlen", "array_map", "json_encode", "preg_match", "str_contains"]) {
    assert.ok(new RegExp(builtin.match).test(name + "("), `${name} not highlighted`);
  }
});

test("predefined constants are highlighted", () => {
  const patterns = grammar.repository.constants.patterns;
  const c = patterns.find((p) => p.name === "constant.other.predefined.hphp");
  assert.ok(c, "no constant pattern");
  const re = new RegExp(c.match);
  for (const name of ["GREEN", "PHP_INT_MAX", "JSON_PRETTY_PRINT", "M_PI"]) {
    assert.ok(re.test(name), `${name} not highlighted`);
  }
});

test("variables win over constants (no `$GREEN` half-highlighting)", () => {
  const order = grammar.patterns.map((p) => p.include);
  assert.ok(order.indexOf("#variables") < order.indexOf("#constants"));
  assert.ok(order.indexOf("#variables") < order.indexOf("#functions"));
});

test("language keywords and literals are covered", () => {
  const all = JSON.stringify(grammar.repository);
  for (const word of ["unsafe", "own", "let", "match", "yield", "import", "global", "fn", "export"]) {
    assert.ok(all.includes(word), `keyword ${word} missing from the grammar`);
  }
  assert.equal(grammar.repository.comments.patterns[0].match, "//.*$");
  const nums = JSON.stringify(grammar.repository.numbers);
  assert.ok(nums.includes("0[xX]"), "hex numbers missing");
  assert.ok(nums.includes("0[bB]"), "binary numbers missing");
});

test("classes and member access have themeable PHP scopes", () => {
  const order = grammar.patterns.map((p) => p.include);
  assert.ok(order.indexOf("#classes") < order.indexOf("#keywords"));
  assert.ok(order.includes("#members"));
  assert.ok(grammar.repository.classes.patterns.some((p) => p.captures?.[2]?.name === "entity.name.type.class.php"));
  assert.ok(grammar.repository.members.patterns.some((p) => p.name === "entity.name.function.member.php"));
  assert.ok(grammar.repository.members.patterns.some((p) => p.name === "variable.other.property.php"));
});

test("function names and pub use dedicated syntax scopes", () => {
  const declaration = grammar.repository.keywords.patterns.find((p) => p.name === "meta.function.declaration.hphp");
  assert.ok(declaration, "function declaration rule missing");
  const match = new RegExp(declaration.match).exec("function greet(string $name)");
  assert.equal(match?.[2], "greet");
  assert.equal(declaration.captures[2].name, "entity.name.function.hphp");
  const modifiers = grammar.repository.keywords.patterns.find((p) => p.name === "keyword.other.hphp");
  assert.ok(new RegExp(modifiers.match).test("pub"), "pub should use the modifier scope");
});

test("custom function calls use the PHP function-name scope", () => {
  const rules = grammar.repository.functions.patterns;
  const builtinIndex = rules.findIndex((p) => p.name === "support.function.builtin.hphp");
  const customIndex = rules.findIndex((p) => p.name === "entity.name.function.php");
  assert.ok(builtinIndex >= 0 && customIndex > builtinIndex, "builtins must keep their own scope");
  const call = new RegExp(rules[customIndex].match);
  assert.ok(call.test("greet($name)"));
  assert.ok(!call.test("$callback($name)"));
  assert.ok(!call.test("$animal->speak()"));
  assert.equal(rules[customIndex].name, "entity.name.function.php");
});

test("the hash comment does not swallow attributes", () => {
  const rule = grammar.repository.comments.patterns.find((p) => /number-sign/.test(p.name));
  assert.ok(rule);
  assert.ok(!new RegExp(rule.match).test("#[Attr]"), "#[Attr] treated as a comment");
  assert.ok(new RegExp(rule.match).test("# note"));
});
