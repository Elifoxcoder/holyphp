#!/usr/bin/env node
/* genbuiltinsites.js — generates one documentation page per builtin under
 * docs/builtin/<name>.html from builtins.json + builtin-examples.js.
 * Also patches docs/builtins.html so every signature row links to its page.
 * Run after genext.js.  node scripts/genbuiltinsites.js
 */
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const data = JSON.parse(
  fs.readFileSync(path.join(root, "extension/hphp/data/builtins.json"), "utf8")
);
const EX = require(path.join(__dirname, "builtin-examples.js"));
const builtins = data.builtins || [];
const byName = new Map(builtins.map(b => [b.name, b]));
const names = builtins.map(b => b.name);

const esc = s => String(s)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

/* page style — self-contained (same fallback theme as builtins.html) */
const STYLE = `
:root { --bg: #14151a; --bg2: #1b1c23; --bg3: #22242d; --fg: #e8e6e3; --dim: #9a97a3;
        --line: #2c2e38; --purple: #8892bf; --purple-hi: #aeb6e8; --ember: #ff7b3d;
        --green: #7ec699; --code-bg: #101116;
        --mono: "Cascadia Code", "JetBrains Mono", Consolas, Menlo, monospace;
        --sans: system-ui, "Segoe UI", sans-serif; }
body { margin: 0; background: var(--bg); color: var(--fg); font-family: var(--sans); line-height: 1.55; }
main { max-width: 860px; margin: 0 auto; padding: 28px 24px 80px; }
a { color: var(--purple-hi); }
h1 { font-size: 26px; margin: 0 0 4px; }
h1 .n { color: #61afef; }
h2 { font-size: 15px; text-transform: uppercase; letter-spacing: .08em; color: var(--dim);
     margin: 30px 0 8px; }
.crumbs { font-size: 13px; margin-bottom: 22px; }
.crumbs a { text-decoration: none; }
.badge { font-size: .68rem; font-weight: 700; letter-spacing: .4px; text-transform: uppercase;
         color: #e06c75; border: 1px solid #e06c75; border-radius: 3px; padding: 0 4px;
         margin-left: 8px; vertical-align: 3px; }
.sig { font-family: var(--mono); font-size: 15px; background: var(--bg2);
       border: 1px solid var(--line); border-radius: 8px; padding: 12px 16px; margin: 8px 0 0; }
.sig .n { color: #61afef; font-weight: 600; }
.sig .r { color: var(--green); }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th, td { text-align: left; padding: 7px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { color: var(--dim); font-weight: 600; font-size: 12.5px; text-transform: uppercase; letter-spacing: .06em; }
td.pn { font-family: var(--mono); color: #61afef; white-space: nowrap; }
td.pt { font-family: var(--mono); color: var(--green); white-space: nowrap; }
.opt { font-size: 11px; color: var(--dim); border: 1px solid var(--line); border-radius: 3px;
       padding: 0 4px; margin-left: 6px; }
pre { background: var(--code-bg); border: 1px solid var(--line); border-radius: 8px;
      padding: 14px 16px; overflow-x: auto; font-family: var(--mono); font-size: 13.5px;
      line-height: 1.5; margin: 8px 0; }
pre.out { color: var(--green); }
pre.out::before { content: "output"; display: block; font-size: 10px; text-transform: uppercase;
                  letter-spacing: .1em; color: var(--dim); margin-bottom: 6px; }
.ret { color: var(--fg); }
.see a { display: inline-block; background: var(--bg3); border: 1px solid var(--line);
         border-radius: 20px; padding: 2px 12px; margin: 0 6px 6px 0; font-family: var(--mono);
         font-size: 13px; text-decoration: none; }
.see a:hover { border-color: var(--purple); }
.nav { display: flex; justify-content: space-between; margin-top: 40px; padding-top: 16px;
       border-top: 1px solid var(--line); font-size: 13.5px; }
.nav a { text-decoration: none; }
.nav .mid { color: var(--dim); }
.muted { color: var(--dim); }
`;

function page(name, prev, next) {
  const b = byName.get(name);
  const e = EX[name] || {};
  const sig = b ? b.sig : `${name}()`;
  const sigHtml = esc(sig)
    .replace(new RegExp("^(" + name + ")"), '<span class="n">$1</span>')
    .replace(/:\s*(\S+)$/, ': <span class="r">$1</span>');

  /* parameter table */
  let paramsHtml = "";
  if (e.params && e.params.length) {
    const rows = e.params.map(p => {
      const req = p.req === false
        ? '<span class="opt">optional</span>'
        : (p.n === "..." ? "" : "");
      return `<tr><td class="pn">$${esc(p.n)}</td><td class="pt">${esc(p.t)}</td>` +
             `<td>${esc(p.d)}${req}</td></tr>`;
    }).join("\n");
    paramsHtml = `<h2>Parameters</h2><table>
<tr><th>Name</th><th>Type</th><th>Description</th></tr>
${rows}
</table>`;
  } else {
    paramsHtml = `<h2>Parameters</h2><p class="muted">Takes no arguments.</p>`;
  }

  /* example */
  let exampleHtml = "";
  if (e.ex) {
    exampleHtml = `<h2>Example</h2><pre>${esc(e.ex)}</pre>`;
    if (e.out) exampleHtml += `<pre class="out">${esc(e.out)}</pre>`;
    if (e.nocode) exampleHtml += `<p class="muted">This call needs a real environment (window, server, database or stdin) — the snippet above shows the typical usage shape.</p>`;
  }

  const retHtml = e.ret ? `<h2>Return value</h2><p class="ret">${esc(e.ret)}</p>` : "";

  /* see also */
  const see = (e.see || []).filter(s => byName.has(s));
  const seeHtml = see.length
    ? `<h2>See also</h2><div class="see">${see.map(s => `<a href="${s}.html">${s}()</a>`).join("")}</div>`
    : "";

  const navRow = `<div class="nav">
  <a href="${prev}.html">&larr; ${prev}()</a>
  <span class="mid">${names.indexOf(name) + 1} / ${names.length}</span>
  <a href="${next}.html">${next}() &rarr;</a>
</div>`;

  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${name}() — HolyPHP built-ins</title>
<style>${STYLE}</style>
</head>
<body>
<main>
  <div class="crumbs"><a href="../builtins.html">&larr; All built-ins</a> / ${esc(name)}()</div>
  <h1><span class="n">${esc(name)}</span>()</h1>
  ${b && b.unsafe ? '<span class="badge">unsafe</span>' : ""}
  <div class="sig">${sigHtml}</div>
  ${paramsHtml}
  ${retHtml}
  ${exampleHtml}
  ${seeHtml}
  ${navRow}
</main>
</body>
</html>
`;
}

/* generate all pages */
const outDir = path.join(root, "docs", "builtin");
fs.mkdirSync(outDir, { recursive: true });

/* clean stale pages */
for (const f of fs.readdirSync(outDir)) {
  if (f.endsWith(".html")) fs.unlinkSync(path.join(outDir, f));
}

let count = 0;
for (let i = 0; i < names.length; i++) {
  const name = names[i];
  const prev = names[(i - 1 + names.length) % names.length];
  const next = names[(i + 1) % names.length];
  fs.writeFileSync(path.join(outDir, name + ".html"), page(name, prev, next));
  count++;
}

/* link every row on the index page to its subpage */
const indexPath = path.join(root, "docs", "builtins.html");
if (fs.existsSync(indexPath)) {
  let idx = fs.readFileSync(indexPath, "utf8");
  idx = idx.replace(/<div class="bi-row" id="fn-([a-zA-Z0-9_]+)" data-name="/g,
    (_m, n) => `<div class="bi-row" id="fn-${n}" data-linked="1" onclick="location.href='builtin/${n}.html'" style="cursor:pointer" data-name="`);
  fs.writeFileSync(indexPath, idx);
}

console.log(`genbuiltinsites: wrote ${count} pages to docs/builtin/ (index linked)`);
