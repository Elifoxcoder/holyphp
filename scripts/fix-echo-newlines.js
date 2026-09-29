#!/usr/bin/env node
/* fix-echo-newlines.js — echo does not append newlines (PHP semantics), so
 * multi-line documented outputs need explicit ", \n" prints in the examples.
 * Quote-aware: only touches `ex:` strings of entries whose `out:` is
 * multi-line, and only statements that START with `echo`.
 * Run:  node scripts/fix-echo-newlines.js
 */
const fs = require("fs");
const path = require("path");

const p = path.join(__dirname, "builtin-examples.js");
const src = fs.readFileSync(p, "utf8");
const lines = src.split("\n");

/* find entry blocks: "  name: {" ... "  }," at exactly 2-space indent */
let changed = 0, touched = 0;
for (let i = 0; i < lines.length; i++) {
  const m = lines[i].match(/^  ([A-Za-z_][A-Za-z0-9_]*): \{$/);
  if (!m) continue;
  /* block end: next "  }," at same indent */
  let j = i + 1;
  while (j < lines.length && lines[j] !== "  },") {
    if (/^  [A-Za-z_][A-Za-z0-9_]*: \{$/.test(lines[j])) { j = -1; break; }
    j++;
  }
  if (j === -1 || j >= lines.length) continue;
  const block = lines.slice(i, j + 1);

  /* only entries whose documented output is multi-line */
  const outLine = block.find(l => l.trimStart().startsWith("out:"));
  if (!outLine || !outLine.includes("\\n")) continue;

  const exIdx = block.findIndex(l => l.trimStart().startsWith("ex:"));
  if (exIdx < 0) continue;
  const exLine = block[exIdx];
  const em = exLine.match(/^(\s*)ex: '(.*)',?$/);
  if (!em) continue;                       // multi-line ex — leave alone

  /* quote-aware statement split on unquoted \n escapes */
  const body = em[2];
  const stmts = [];
  let cur = "", q = null;
  for (let k = 0; k < body.length; k++) {
    const c = body[k];
    if (q) {
      cur += c;
      if (c === "\\") { cur += body[k + 1] ?? ""; k++; }
      else if (c === q) q = null;
      continue;
    }
    if (c === '"' || c === "'") { q = c; cur += c; continue; }
    if (c === "\\" && body[k + 1] === "n") { stmts.push(cur); cur = ""; k++; continue; }
    cur += c;
  }
  if (cur.trim()) stmts.push(cur);

  let mod = false;
  const fixed = stmts.map(st => {
    const t = st.trim();
    if (!/^echo[ (]/.test(t)) return st;
    /* already ends with an explicit newline print? */
    if (/, ?"\\n";$/.test(t) || /, ?'\\n';$/.test(t) || /\. ?"\\n";$/.test(t) ||
        /PHP_EOL;$/.test(t) || /"\\n";$/.test(t) && /, ?"\\n";$/.test(t)) return st;
    /* append , "\n" before the trailing ; */
    const trailing = st.match(/;$/, "");
    if (!trailing) return st;
    mod = true;
    return st.slice(0, -1) + ', "\\n";';
  });

  if (mod) {
    touched++;
    changed += fixed.filter((s, x) => s !== stmts[x]).length;
    block[exIdx] = em[1] + "ex: '" + fixed.join("\\n") + "',";
    lines.splice(i, block.length, ...block);
    i += block.length - 1;
  }
}

fs.writeFileSync(p, lines.join("\n"), "utf8");
console.log(`entries touched: ${touched}, echo statements changed: ${changed}`);
