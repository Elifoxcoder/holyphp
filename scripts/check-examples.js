#!/usr/bin/env node
/* check-examples.js — runs every runnable example from builtin-examples.js
 * through ./hphp.exe and compares stdout with the documented output.
 * Entries with nocode: true are skipped (need a real environment).
 *
 *   node scripts/check-examples.js           run all
 *   node scripts/check-examples.js strstr    run one
 */
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

const root = path.join(__dirname, "..");
const EX = require(path.join(__dirname, "builtin-examples.js"));
const data = JSON.parse(
  fs.readFileSync(path.join(root, "extension/hphp/data/builtins.json"), "utf8")
);
const known = new Set(data.builtins.map(b => b.name));

const filters = process.argv.slice(2);   // zero, one or many names
const hphp = path.join(root, "hphp.exe");

/* normalize: hphp on Windows prints \r\n — compare line-wise, trimmed */
const norm = s => String(s).replace(/\r\n/g, "\n").replace(/\r/g, "\n")
  .split("\n").map(l => l.replace(/\s+$/, "")).join("\n").replace(/\n+$/, "");

let pass = 0, fail = 0, skipped = 0;
const failures = [];

for (const [name, e] of Object.entries(EX)) {
  if (filters.length && !filters.includes(name)) continue;
  if (!known.has(name)) continue;             // not a registered builtin
  if (e.nocode || !e.ex) { skipped++; continue; }

  const tmp = path.join(os.tmpdir(), "hphp_doc_" + name.replace(/[^a-z0-9_]/gi, "_") + ".hphp");
  fs.writeFileSync(tmp, e.ex + "\n", "utf8");
  let out = "", err = "", code = 0;
  try {
    /* stdout/stderr go to files: if the timeout kills hphp while its gcc
     * child is running, the orphaned gcc would keep a pipe open forever. */
    const o = path.join(os.tmpdir(), "hphp_doc_out.txt");
    const er = path.join(os.tmpdir(), "hphp_doc_err.txt");
    const fdO = fs.openSync(o, "w"), fdE = fs.openSync(er, "w");
    execFileSync(hphp, ["run", tmp], { timeout: 10000, stdio: ["ignore", fdO, fdE] });
    fs.closeSync(fdO); fs.closeSync(fdE);
    out = fs.readFileSync(o, "utf8");
    err = fs.readFileSync(er, "utf8");
  } catch (ex2) {
    code = ex2.status || 1;
    try { out = fs.readFileSync(path.join(os.tmpdir(), "hphp_doc_out.txt"), "utf8"); } catch (_) {}
    try { err = fs.readFileSync(path.join(os.tmpdir(), "hphp_doc_err.txt"), "utf8"); } catch (_) {}
  }
  const got = norm(out), want = norm(e.out);
  if (code === 0 && got === want) {
    pass++;
    console.log(`PASS ${name}`);
  } else {
    fail++;
    console.log(`FAIL ${name}`);
    if (code !== 0) console.log(`     exit=${code} ${err.split("\n")[0].slice(0, 120)}`);
    console.log(`     want: ${JSON.stringify(want).slice(0, 140)}`);
    console.log(`     got:  ${JSON.stringify(got).slice(0, 140)}`);
    failures.push(name);
  }
}

console.log(`\n== ${pass} passed, ${fail} failed, ${skipped} skipped (env-dependent) ==`);
if (fail) process.exit(1);
