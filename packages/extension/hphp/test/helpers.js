/* test/helpers.js — shared fixtures for the extension tests. */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

let seq = 0;

/* Write a file into a fresh temp directory and return its path. */
function fixture(source, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hphp-ext-"));
  const file = path.join(dir, name || `f${++seq}.hphp`);
  fs.writeFileSync(file, source, "utf8");
  return file;
}

/* The compiler to test against: $HPHP, else `hphp` from PATH. */
function hphpPath() {
  return process.env.HPHP || "hphp";
}

function hphpAvailable() {
  const r = spawnSync(hphpPath(), ["--version"], { encoding: "utf8" });
  return !r.error && r.status === 0;
}

/* Run `hphp check <file>` and return {status, output}. */
function check(file) {
  const r = spawnSync(hphpPath(), ["check", file], { encoding: "utf8" });
  return {
    status: r.status,
    output: String(r.stdout || "") + String(r.stderr || ""),
    error: r.error || null,
  };
}

module.exports = { fixture, hphpPath, hphpAvailable, check };
