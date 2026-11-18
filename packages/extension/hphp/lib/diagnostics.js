/* lib/diagnostics.js — turn `hphp check` output into diagnostics.
 *
 * The compiler has two error styles:
 *   sema:   file.hphp:12:5: error: cannot assign string to int
 *   fatal:  hphpc: error: file.hphp:4:1: unexpected token '}' in expression
 * (and both can appear for imported files). Everything here is pure so the
 * mapping is covered by tests without an editor.
 */
"use strict";

const path = require("path");

const LOCATED = /^(.*?):(\d+):(\d+):\s*(error|warning|note):\s*(.*)$/;
const LOCATED_NO_COL = /^(.*?):(\d+):\s*(error|warning|note):\s*(.*)$/;
const FATAL = /^hphpc?:\s*(error|warning):\s*(.*)$/;
const LOCATED_MSG = /^(.*?):(\d+):(\d+):\s*(.*)$/;
const NOISE = /^(hphp: compilation failed|.*: OK \(types and borrows check\))\s*$/;

/* Parse one line into {file,line,col,severity,message} or null. */
function parseLine(raw) {
  const line = raw.replace(/\r$/, "").trimEnd();
  if (!line || NOISE.test(line)) return null;

  let m = LOCATED.exec(line);
  if (m) {
    return { file: m[1], line: +m[2], col: +m[3], severity: m[4], message: m[5].trim() };
  }
  m = LOCATED_NO_COL.exec(line);
  if (m) {
    return { file: m[1], line: +m[2], col: 0, severity: m[3], message: m[4].trim() };
  }
  m = FATAL.exec(line);
  if (m) {
    const rest = m[2].trim();
    const loc = LOCATED_MSG.exec(rest);
    if (loc) {
      return {
        file: loc[1],
        line: +loc[2],
        col: +loc[3],
        severity: m[1],
        message: loc[4].trim(),
      };
    }
    return { file: null, line: 0, col: 0, severity: m[1], message: rest };
  }
  return null;
}

/* Parse a whole compiler run. */
function parseCompilerOutput(output) {
  const out = [];
  for (const raw of String(output || "").split(/\r?\n/)) {
    const d = parseLine(raw);
    if (d) out.push(d);
  }
  return out;
}

/* Compare two paths the way the platform does (Windows is case-insensitive). */
function samePath(a, b) {
  if (!a || !b) return false;
  const na = path.resolve(a);
  const nb = path.resolve(b);
  if (process.platform === "win32") return na.toLowerCase() === nb.toLowerCase();
  return na === nb;
}

/* The first quoted token in a compiler message ('}' in "unexpected token '}'"). */
function quotedToken(message) {
  const m = /'([^']{1,40})'/.exec(message || "");
  return m ? m[1] : "";
}

/* Range (0-based, end-exclusive) for a 1-based compiler location.
 * `lines` is the document split into lines. */
function rangeFor(lines, line1, col1, message) {
  const last = Math.max(0, lines.length - 1);
  const line = Math.max(0, Math.min((line1 || 1) - 1, last));
  const text = lines[line] || "";

  let start = col1 > 0 ? col1 - 1 : 0;
  if (start > text.length) start = text.length;

  /* Prefer the token the message quotes, when it sits on this line. */
  const token = quotedToken(message);
  if (token) {
    let at = text.indexOf(token, start);
    if (at < 0) at = text.indexOf(token);
    if (at >= 0) return { line, start: at, end: at + token.length };
  }

  if (col1 <= 0 || start >= text.length) {
    return { line, start: 0, end: Math.max(1, text.length) };
  }
  let end = start;
  if (/[A-Za-z0-9_$]/.test(text[start])) {
    while (end < text.length && /[A-Za-z0-9_$]/.test(text[end])) end++;
  } else if (text[start] === '"' || text[start] === "'") {
    const quote = text[start];
    end = start + 1;
    while (end < text.length && text[end] !== quote) {
      if (text[end] === "\\") end++;
      end++;
    }
    if (end < text.length) end++;
  } else {
    end = Math.min(text.length, start + 1);
  }
  if (end <= start) end = Math.min(text.length, start + 1);
  return { line, start, end };
}

module.exports = {
  parseLine,
  parseCompilerOutput,
  samePath,
  rangeFor,
  quotedToken,
};
