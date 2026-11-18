/* lib/signature.js — which call is the cursor in, and which argument?
 *
 * Pure: given the document text and an offset, find the innermost unclosed
 * `(` and the callee name before it. The extension turns that into
 * vscode.SignatureHelp using the builtin table or the document's functions.
 */
"use strict";

const { findOpenParen, countTopLevelCommas } = require("./text");

/* Keywords that take parentheses but are not calls. */
const CONTROL = new Set([
  "if", "elseif", "while", "for", "foreach", "switch", "match", "catch",
  "function", "fn", "isset", "empty",
]);

/* {name, open, active} or null. `active` is 0-based. */
function signatureAt(text, offset) {
  const open = findOpenParen(text, offset);
  if (open < 0) return null;
  const before = text.slice(0, open);
  const m = /([A-Za-z_]\w*)\s*$/.exec(before);
  if (!m) return null;
  const name = m[1];
  if (CONTROL.has(name)) return null;
  return {
    name,
    open,
    active: countTopLevelCommas(text, open + 1, offset),
  };
}

module.exports = { signatureAt, CONTROL };
