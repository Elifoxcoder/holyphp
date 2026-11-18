/* lib/text.js — pure text helpers (no `vscode` import).
 *
 * The extension works on `vscode.TextDocument`, but everything that needs to
 * be tested works on a plain string plus an offset. Offsets are UTF-16 code
 * units, exactly like VS Code's positions.
 */
"use strict";

/* Offsets of the first character of every line. */
function lineStarts(text) {
  const starts = [0];
  for (let i = 0; i < text.length; i++) {
    if (text.charCodeAt(i) === 10) starts.push(i + 1);
  }
  return starts;
}

function positionAt(text, offset, starts) {
  const s = starts || lineStarts(text);
  let lo = 0, hi = s.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (s[mid] <= offset) lo = mid;
    else hi = mid - 1;
  }
  return { line: lo, character: offset - s[lo] };
}

function offsetAt(text, line, character, starts) {
  const s = starts || lineStarts(text);
  const l = Math.max(0, Math.min(line, s.length - 1));
  const lineEnd = l + 1 < s.length ? s[l + 1] - 1 : text.length;
  return Math.max(s[l], Math.min(s[l] + Math.max(0, character), lineEnd));
}

/* The line's text without the trailing newline (and without a CR). */
function lineText(text, line, starts) {
  const s = starts || lineStarts(text);
  const l = Math.max(0, Math.min(line, s.length - 1));
  const end = l + 1 < s.length ? s[l + 1] - 1 : text.length;
  let out = text.slice(s[l], end);
  if (out.endsWith("\r")) out = out.slice(0, -1);
  return out;
}

/* VS Code's default word range: identifiers, $variables, digits, dots in
 * numbers are *not* included; `$` is, which is what `$foo` completion needs. */
const WORD_CHAR = /[A-Za-z0-9_$]/;

function wordRangeAt(text, offset) {
  let start = offset, end = offset;
  while (start > 0 && WORD_CHAR.test(text[start - 1])) start--;
  while (end < text.length && WORD_CHAR.test(text[end])) end++;
  return { start, end, word: text.slice(start, end) };
}

/* The text from the start of the line up to `offset`. */
function linePrefix(text, offset, starts) {
  const s = starts || lineStarts(text);
  const p = positionAt(text, offset, s);
  return text.slice(s[p.line], offset);
}

/* Replace comments and string contents with spaces, keeping every newline and
 * every offset. Regex-based structure scanning then cannot match a `function`
 * inside a comment or a `{` inside a string. */
function stripNonCode(text) {
  const out = text.split("");
  let i = 0;
  const n = text.length;
  const blank = (from, to) => {
    for (let k = from; k < to && k < n; k++) {
      if (out[k] !== "\n" && out[k] !== "\r") out[k] = " ";
    }
  };
  while (i < n) {
    const c = text[i];
    const d = i + 1 < n ? text[i + 1] : "";
    if (c === "/" && d === "/") {
      let j = i;
      while (j < n && text[j] !== "\n") j++;
      blank(i, j); i = j;
    } else if (c === "#" && d !== "[") {
      let j = i;
      while (j < n && text[j] !== "\n") j++;
      blank(i, j); i = j;
    } else if (c === "/" && d === "*") {
      let j = i + 2;
      while (j < n && !(text[j] === "*" && text[j + 1] === "/")) j++;
      j = Math.min(n, j + 2);
      blank(i, j); i = j;
    } else if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < n && text[j] !== c) {
        if (text[j] === "\\") j++;
        if (text[j] === "\n") break;
        j++;
      }
      j = Math.min(n, j + 1);
      blank(i + 1, j - 1);
      i = j;
    } else {
      i++;
    }
  }
  return out.join("");
}

/* Count top-level commas between two offsets, ignoring (), [], {} and strings.
 * Used for signature help's active parameter. */
function countTopLevelCommas(text, from, to) {
  let depth = 0, count = 0;
  for (let i = from; i < to; i++) {
    const c = text[i];
    if (c === '"' || c === "'") {
      i++;
      while (i < to && text[i] !== c) {
        if (text[i] === "\\") i++;
        i++;
      }
    } else if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth--;
    else if (c === "," && depth === 0) count++;
  }
  return count;
}

/* Index of the innermost unclosed `(` before `offset`, or -1.
 *
 * Forward scan, skipping strings and comments: a backwards scan cannot tell an
 * opening quote from a closing one and would jump across lines into an earlier
 * string literal. */
function findOpenParen(text, offset) {
  const stack = [];
  for (let i = 0; i < offset; i++) {
    const c = text[i];
    if (c === '"' || c === "'") {
      i++;
      while (i < offset && text[i] !== c) {
        if (text[i] === "\\") i++;
        i++;
      }
    } else if (c === "/" && text[i + 1] === "/") {
      while (i < offset && text[i] !== "\n") i++;
    } else if (c === "/" && text[i + 1] === "*") {
      i += 2;
      while (i < offset && !(text[i] === "*" && text[i + 1] === "/")) i++;
      i++;
    } else if (c === "#" && text[i + 1] !== "[") {
      while (i < offset && text[i] !== "\n") i++;
    } else if (c === "(") {
      stack.push(i);
    } else if (c === ")") {
      stack.pop();
    }
  }
  return stack.length ? stack[stack.length - 1] : -1;
}

module.exports = {
  lineStarts,
  positionAt,
  offsetAt,
  lineText,
  wordRangeAt,
  linePrefix,
  stripNonCode,
  countTopLevelCommas,
  findOpenParen,
  WORD_CHAR,
};
