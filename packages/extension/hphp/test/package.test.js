/* test/package.test.js — the VSIX manifest must point at files that exist. */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("fs");
const path = require("path");

const dir = path.join(__dirname, "..");
const pkg = JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8"));

test("manifest basics", () => {
  assert.equal(pkg.name, "holyphp");
  assert.equal(pkg.publisher, "holyphp");
  assert.match(pkg.version, /^\d+\.\d+\.\d+$/);
  assert.ok(pkg.engines && pkg.engines.vscode);
  assert.equal(pkg.main, "./extension.bundle.js");
  assert.ok(fs.existsSync(path.join(dir, "server.bundle.js")), "missing bundled language server");
});

test("every contributed path exists", () => {
  const c = pkg.contributes;
  assert.ok(c, "no contributes block");
  for (const lang of c.languages || []) {
    if (lang.configuration) {
      assert.ok(fs.existsSync(path.join(dir, lang.configuration)), lang.configuration);
    }
  }
  for (const g of c.grammars || []) {
    assert.ok(fs.existsSync(path.join(dir, g.path)), g.path);
  }
  for (const s of c.snippets || []) {
    assert.ok(fs.existsSync(path.join(dir, s.path)), s.path);
  }
  assert.ok(fs.existsSync(path.join(dir, pkg.main)), pkg.main);
});

test("the language is registered for .hphp", () => {
  const lang = (pkg.contributes.languages || []).find((l) => l.id === "holyphp");
  assert.ok(lang, "no holyphp language");
  assert.ok(lang.extensions.includes(".hphp"));
  assert.ok(lang.aliases.includes("HolyPHP"));
});

test("settings are documented and have defaults", () => {
  const props = (pkg.contributes.configuration || {}).properties || {};
  for (const key of ["holyphp.hphpPath", "holyphp.diagnostics", "holyphp.diagnosticsMode", "holyphp.diagnosticsDebounce", "holyphp.maxIndexedFiles"]) {
    assert.ok(props[key], `missing setting ${key}`);
    assert.ok("default" in props[key], `setting ${key} has no default`);
    assert.ok(props[key].description, `setting ${key} has no description`);
  }
});

test("commands are declared", () => {
  const ids = (pkg.contributes.commands || []).map((c) => c.command);
  assert.ok(ids.includes("holyphp.check"));
  assert.ok(ids.includes("holyphp.run"));
  assert.ok(ids.includes("holyphp.builtins"));
});

test("snippets are well formed and prefixes are unique", () => {
  const snip = JSON.parse(fs.readFileSync(path.join(dir, "snippets", "hphp.json"), "utf8"));
  const prefixes = new Set();
  for (const [name, s] of Object.entries(snip)) {
    assert.ok(s.prefix, `${name} has no prefix`);
    assert.ok(!prefixes.has(s.prefix), `duplicate snippet prefix ${s.prefix}`);
    prefixes.add(s.prefix);
    assert.ok(Array.isArray(s.body) && s.body.length > 0, `${name} has no body`);
  }
});

test("lib modules load without vscode", () => {
  for (const f of ["data", "text", "symbols", "diagnostics", "complete", "hover", "signature", "format"]) {
    const mod = require(path.join(dir, "lib", f + ".js"));
    assert.ok(mod && typeof mod === "object", `${f}.js did not export`);
  }
});
