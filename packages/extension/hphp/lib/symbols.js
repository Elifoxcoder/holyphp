/* lib/symbols.js — best-effort structure of one HolyPHP file.
 *
 * A real parser lives in the compiler; this is the editor's quick view: which
 * functions, classes, methods, fields, constants, imports and variables a file
 * declares. It runs on every keystroke, so it is regex-based over a copy of the
 * text with comments and strings blanked out (lib/text.stripNonCode).
 */
"use strict";

const { stripNonCode } = require("./text");

/* Match the brace that closes the one at `open` (index of "{"). */
function matchBrace(code, open) {
  let depth = 0;
  for (let i = open; i < code.length; i++) {
    if (code[i] === "{") depth++;
    else if (code[i] === "}") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return code.length - 1;
}

function lineOf(text, offset) {
  let line = 0;
  for (let i = 0; i < offset && i < text.length; i++) {
    if (text.charCodeAt(i) === 10) line++;
  }
  return line;
}

/* "int $a, string $b = \"x\"" -> [{name,type,optional,default}] */
function parseParams(raw) {
  if (!raw || !raw.trim()) return [];
  const out = [];
  let depth = 0, cur = "";
  const parts = [];
  for (const ch of raw) {
    if (ch === "<" || ch === "[" || ch === "(") depth++;
    else if (ch === ">" || ch === "]" || ch === ")") depth--;
    if (ch === "," && depth === 0) { parts.push(cur); cur = ""; }
    else cur += ch;
  }
  if (cur.trim()) parts.push(cur);
  for (const part of parts) {
    let s = part.trim();
    if (!s) continue;
    const eq = s.indexOf("=");
    const optional = eq >= 0;
    if (optional) s = s.slice(0, eq).trim();
    const m = /(\$[A-Za-z_]\w*)\s*$/.exec(s);
    if (!m) continue;
    const type = s.slice(0, m.index).trim() || "mixed";
    out.push({ name: m[1], type, optional });
  }
  return out;
}

/* Collect the document's declarations. */
function collectSymbols(text) {
  const code = stripNonCode(text);
  const classes = [];
  const functions = [];
  const constants = [];
  const imports = [];
  const variables = [];

  /* classes / interfaces / traits / enums, with their members */
  const classRe = /\b(class|interface|trait|enum)\s+([A-Za-z_]\w*)([^{]*)\{/g;
  let m;
  while ((m = classRe.exec(code)) !== null) {
    const open = code.indexOf("{", m.index + m[0].length - 1);
    const close = matchBrace(code, open);
    const body = code.slice(open + 1, close);
    const bodyOffset = open + 1;
    const cls = {
      name: m[2],
      kind: m[1],
      line: lineOf(text, m.index),
      offset: m.index,
      nameOffset: m.index + m[0].indexOf(m[2]),
      endOffset: close,
      extends: (/extends\s+([A-Za-z_]\w*)/.exec(m[3] || "") || [])[1] || "",
      methods: [],
      fields: [],
      constants: [],
    };

    const methodRe = /\bfunction\s+([A-Za-z_]\w*)\s*\(([^)]*)\)(?:\s*:\s*([A-Za-z_][\w<>\[\]|?]*))?/g;
    let mm;
    while ((mm = methodRe.exec(body)) !== null) {
      cls.methods.push({
        name: mm[1],
        params: parseParams(mm[2]),
        paramsRaw: (mm[2] || "").trim(),
        ret: mm[3] || "mixed",
        line: lineOf(text, bodyOffset + mm.index),
        offset: bodyOffset + mm.index,
        nameOffset: bodyOffset + mm.index + mm[0].indexOf(mm[1]),
        static: /\bstatic\s+$/.test(body.slice(Math.max(0, mm.index - 20), mm.index)),
      });
    }

    const fieldRe = /\b(?:public|protected|private|static|readonly)\s+(?:static\s+)?([A-Za-z_][\w<>\[\]|?]*)\s+\$([A-Za-z_]\w*)/g;
    let fm;
    while ((fm = fieldRe.exec(body)) !== null) {
      cls.fields.push({
        name: fm[2],
        type: fm[1],
        line: lineOf(text, bodyOffset + fm.index),
        nameOffset: bodyOffset + fm.index + fm[0].lastIndexOf(fm[2]),
      });
    }

    const constRe = /\bconst\s+([A-Za-z_]\w*)/g;
    let cm;
    while ((cm = constRe.exec(body)) !== null) {
      cls.constants.push({ name: cm[1], line: lineOf(text, bodyOffset + cm.index), nameOffset: bodyOffset + cm.index + cm[0].indexOf(cm[1]) });
    }

    classes.push(cls);
  }

  /* functions outside every class body */
  const fnRe = /\bfunction\s+([A-Za-z_]\w*)\s*\(([^)]*)\)(?:\s*:\s*([A-Za-z_][\w<>\[\]|?]*))?/g;
  while ((m = fnRe.exec(code)) !== null) {
    const inside = classes.some((c) => m.index > c.offset && m.index < c.endOffset);
    if (inside) continue;
    functions.push({
      name: m[1],
      params: parseParams(m[2]),
      paramsRaw: (m[2] || "").trim(),
      ret: m[3] || "mixed",
      line: lineOf(text, m.index),
      offset: m.index,
      nameOffset: m.index + m[0].indexOf(m[1]),
    });
  }

  /* top-level constants */
  const constRe = /\bconst\s+([A-Za-z_]\w*)/g;
  while ((m = constRe.exec(code)) !== null) {
    if (classes.some((c) => m.index > c.offset && m.index < c.endOffset)) continue;
    constants.push({ name: m[1], line: lineOf(text, m.index), offset: m.index, nameOffset: m.index + m[0].indexOf(m[1]) });
  }

  /* imports / use — read the name from the original text (the string content
   * is blanked in `code`), but only where the keyword is real code. */
  const impRe = /\b(?:import|use)\s+["']([^"']+)["']/g;
  while ((m = impRe.exec(text)) !== null) {
    if (code[m.index] !== text[m.index]) continue;
    imports.push({ name: m[1], line: lineOf(text, m.index), offset: m.index });
  }

  /* variables: `let $x: T`, `$x = ...` and parameters */
  const seen = new Set();
  const letRe = /\blet\s+\$([A-Za-z_]\w*)(?:\s*:\s*([A-Za-z_][\w<>\[\]|?]*))?/g;
  while ((m = letRe.exec(code)) !== null) {
    seen.add(m[1]);
    variables.push({ name: m[1], type: m[2] || "", line: lineOf(text, m.index), declared: true });
  }
  const assignRe = /\$([A-Za-z_]\w*)\s*(?::\s*([A-Za-z_][\w<>\[\]|?]*))?\s*=(?!=)/g;
  while ((m = assignRe.exec(code)) !== null) {
    if (seen.has(m[1])) continue;
    seen.add(m[1]);
    variables.push({ name: m[1], type: m[2] || "", line: lineOf(text, m.index), declared: false });
  }
  const paramRe = /\$([A-Za-z_]\w*)/g;
  while ((m = paramRe.exec(code)) !== null) {
    if (seen.has(m[1])) continue;
    seen.add(m[1]);
    variables.push({ name: m[1], type: "", line: lineOf(text, m.index), declared: false });
  }

  return { classes, functions, constants, imports, variables };
}

/* The class whose body contains `offset`, if any. */
function enclosingClass(symbols, offset) {
  let best = null;
  for (const c of symbols.classes) {
    if (offset >= c.offset && offset <= c.endOffset) {
      if (!best || c.endOffset - c.offset < best.endOffset - best.offset) best = c;
    }
  }
  return best;
}

/* The declared type of `$name` at `offset`, best effort:
 *   let $x: T      $x = new T(...)     T $x (parameter) */
function variableType(symbols, text, name, offset) {
  const esc = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const code = stripNonCode(text);
  let m = new RegExp("\\blet\\s+\\$" + esc + "\\s*:\\s*([A-Za-z_][\\w<>\\[\\]|?]*)").exec(code);
  if (m) return m[1];
  m = new RegExp("\\$" + esc + "\\s*=\\s*new\\s+([A-Za-z_][\\w]*)").exec(code);
  if (m) return m[1];
  m = new RegExp("([A-Za-z_][\\w<>\\[\\]|?]*)\\s+\\$" + esc + "\\b").exec(code);
  if (m && !/^(if|while|for|foreach|switch|return|echo|print|match)$/.test(m[1])) return m[1];
  return "";
}

module.exports = { collectSymbols, enclosingClass, variableType, parseParams, matchBrace };
