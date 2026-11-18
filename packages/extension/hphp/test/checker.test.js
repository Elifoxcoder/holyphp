/* test/checker.test.js — the whole live-diagnostics path with the real
 * compiler: dirty buffer -> temp file -> `hphp check` -> mapped diagnostics.
 *
 * Skipped when no hphp is on PATH. */
"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("path");
const fs = require("fs");
const os = require("os");
const { spawnSync } = require("child_process");

const mock = require("./mock-vscode");
mock.install();

const HPHP = process.env.HPHP || "hphp";
const hasHphp = !spawnSync(HPHP, ["--version"], { encoding: "utf8" }).error;

const ext = require(path.join(__dirname, "..", "extension.js"));
const { Checker } = ext._internals;
const { Uri, Range } = mock;

function makeDoc(text, fsPath, dirty) {
  const lines = text.split(/\r?\n/);
  return {
    languageId: "holyphp",
    version: 1,
    isDirty: !!dirty,
    isClosed: false,
    uri: new Uri(fsPath),
    lineCount: lines.length,
    getText: () => text,
    lineAt: (n) => ({ text: lines[Math.max(0, Math.min(n, lines.length - 1))], range: new Range(n, 0, n, 0) }),
  };
}

function waitFor(predicate, ms) {
  return new Promise((resolve, reject) => {
    const t0 = Date.now();
    const tick = () => {
      let v;
      try { v = predicate(); } catch (_) { v = false; }
      if (v) return resolve(v);
      if (Date.now() - t0 > ms) return reject(new Error("timeout waiting for the checker"));
      setTimeout(tick, 25);
    };
    tick();
  });
}

const skip = hasHphp ? false : "hphp not on PATH";

test("a dirty buffer with a syntax error gets a mapped diagnostic", { skip }, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hphp-check-"));
  const file = path.join(dir, "main.hphp");
  fs.writeFileSync(file, "<?php\nfunction ok(): int { return 1; }\n", "utf8");

  /* the editor buffer differs from disk: the error exists only in the buffer */
  const text = "<?php\nfunction broken(int $x): int {\n    return $x +\n}\n";
  const doc = makeDoc(text, file, true);
  const collection = mock.vscode.languages.createDiagnosticCollection();
  const status = { text: "", tooltip: "", show() {}, hide() {} };

  const checker = new Checker(collection, status, () => {});
  checker.schedule(doc, true);

  const diags = await waitFor(() => {
    const d = collection.get(doc.uri);
    return d && d.length > 0 ? d : false;
  }, 8000);

  assert.ok(diags.length >= 1, "no diagnostics");
  const d = diags[0];
  assert.equal(d.severity, 0, "not an error");
  assert.equal(d.source, "holyphp");
  assert.ok(d.range.start.line >= 1, `unexpected line ${d.range.start.line}`);
  assert.ok(/token|expression|unexpected/i.test(d.message), d.message);

  /* the temporary file must be gone again */
  const leftovers = fs.readdirSync(dir).filter((f) => f.startsWith(".hphp-check-"));
  assert.deepEqual(leftovers, [], "temp file left behind");
});

test("a clean buffer clears the diagnostics", { skip }, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hphp-check-"));
  const file = path.join(dir, "ok.hphp");
  const doc = makeDoc("<?php\nfunction ok(): int { return 1; }\n", file, true);
  const collection = mock.vscode.languages.createDiagnosticCollection();
  const status = { text: "", tooltip: "", show() {}, hide() {} };

  const checker = new Checker(collection, status, () => {});
  checker.schedule(doc, true);
  /* updateStatus() runs at the end of every check and sets the status text */
  await waitFor(() => status.text !== "", 8000);

  assert.deepEqual(collection.get(doc.uri), []);
});
