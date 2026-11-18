/* test/complete.test.js */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { completionContext, buildItems, classMembers, resolveReceiver } = require("../lib/complete");
const { collectSymbols } = require("../lib/symbols");

const SRC = `<?php
class User {
    public string $name;
    public function greet(string $who): string { return "hi"; }
    public function rename(string $name): void { }
}
class Admin extends User {
    public function ban(int $id): void { }
}
function helper(int $n): int { return $n; }
let $u: User = new User("x");
$u->greet("a");
`;

function ctxAt(text, marker) {
  const offset = text.indexOf(marker) + marker.length;
  return { ctx: completionContext(text, offset), offset };
}

test("member context after ->", () => {
  const { ctx } = ctxAt(SRC, "$u->");
  assert.equal(ctx.kind, "member");
  assert.equal(ctx.receiver, "$u");
  assert.equal(ctx.prefix, "");
});

test("member context with a partial word", () => {
  const { ctx } = ctxAt(SRC, "$u->gre");
  assert.equal(ctx.kind, "member");
  assert.equal(ctx.prefix, "gre");
});

test("static context after ::", () => {
  const text = "User::";
  const ctx = completionContext(text, text.length);
  assert.equal(ctx.kind, "static");
  assert.equal(ctx.receiver, "User");
});

test("class context after new", () => {
  const { ctx } = ctxAt(SRC, "new ");
  assert.equal(ctx.kind, "class");
});

test("builtin snippet inserts required arguments only", () => {
  const text = "number_format(";
  const items = buildItems(completionContext(text, text.length), collectSymbols(text), text, text.length);
  const item = items.find((x) => x.label === "number_format");
  assert.ok(item);
  assert.equal(item.insertText, "number_format(${1:$num})$0");
});

test("type context inside a signature", () => {
  const text = "function f(int $a, ";
  const ctx = completionContext(text, text.length);
  assert.equal(ctx.kind, "type");
});

test("type context after let $x:", () => {
  const text = "let $x: ";
  assert.equal(completionContext(text, text.length).kind, "type");
});

test("general context at the start of a statement", () => {
  const text = "str";
  const ctx = completionContext(text, text.length);
  assert.equal(ctx.kind, "general");
  assert.equal(ctx.prefix, "str");
});

test("receiver resolution: let, param, new, $this", () => {
  const s = collectSymbols(SRC);
  assert.equal(resolveReceiver(s, SRC, "$u", 0), "User");
  assert.equal(resolveReceiver(s, SRC, "Admin", 0), "Admin");
  const inside = SRC.indexOf("return \"hi\"");
  assert.equal(resolveReceiver(s, SRC, "$this", inside), "User");
});

test("classMembers follows extends", () => {
  const s = collectSymbols(SRC);
  const admin = classMembers(s, "Admin").map((m) => m.name);
  assert.ok(admin.includes("ban"));
  assert.ok(admin.includes("greet"), "inherited method missing");
});

test("member items for a typed receiver", () => {
  const s = collectSymbols(SRC);
  const offset = SRC.indexOf("$u->") + 4;
  const items = buildItems({ kind: "member", prefix: "", receiver: "$u" }, s, SRC, offset);
  const labels = items.map((i) => i.label);
  assert.ok(labels.includes("greet"));
  assert.ok(labels.includes("rename"));
  assert.ok(labels.includes("name"));
});

test("class items after new", () => {
  const s = collectSymbols(SRC);
  const items = buildItems({ kind: "class", prefix: "" }, s, SRC, 0);
  assert.deepEqual(items.map((i) => i.label), ["User", "Admin"]);
});

test("type items list the built-in types", () => {
  const s = collectSymbols(SRC);
  const items = buildItems({ kind: "type", prefix: "" }, s, SRC, 0);
  const labels = items.map((i) => i.label);
  assert.ok(labels.includes("int"));
  assert.ok(labels.includes("string"));
  assert.ok(labels.includes("Vec"));
});

test("general items contain variables, functions, builtins and keywords", () => {
  const s = collectSymbols(SRC);
  const items = buildItems({ kind: "general", prefix: "" }, s, SRC, 0);
  const labels = new Set(items.map((i) => i.label));
  assert.ok(labels.has("$u"), "variable missing");
  assert.ok(labels.has("helper"), "user function missing");
  assert.ok(labels.has("strlen"), "builtin missing");
  assert.ok(labels.has("foreach"), "keyword missing");
  assert.ok(labels.has("GREEN"), "constant missing");
});

test("builtins get a snippet with their parameter names", () => {
  const s = collectSymbols("");
  const items = buildItems({ kind: "general", prefix: "" }, s, "", 0);
  const println = items.find((i) => i.label === "println");
  assert.ok(println);
  assert.equal(println.snippet, true);
  assert.match(println.insertText, /\$\{1:\$value\}/);
});

test("constants are not offered as calls", () => {
  const s = collectSymbols("");
  const items = buildItems({ kind: "general", prefix: "" }, s, "", 0);
  const green = items.find((i) => i.label === "GREEN");
  assert.equal(green.kind, "constant");
  assert.equal(green.snippet, false);
  assert.equal(green.insertText, "GREEN");
});
