/* Conservative formatter: normalizes indentation from syntax braces and
 * strips trailing whitespace without rewriting expressions or comments. */
"use strict";
const { stripNonCode } = require("./text");

function formatIndentation(text, tabSize, insertSpaces) {
  const newline = text.includes("\r\n") ? "\r\n" : "\n";
  const sourceLines = text.split(/\r?\n/);
  const codeLines = stripNonCode(text).split(/\r?\n/);
  const unit = insertSpaces === false ? "\t" : " ".repeat(Math.max(1, Math.min(16, tabSize || 4)));
  let depth = 0;
  const formatted = sourceLines.map((line, i) => {
    const code = codeLines[i] || "";
    const body = line.trim();
    if (!body) return "";
    const leading = /^\s*(}+)/.exec(code);
    const closes = leading ? leading[1].length : 0;
    const indent = Math.max(0, depth - closes);
    let balance = 0;
    for (const ch of code) {
      if (ch === "{") balance++;
      else if (ch === "}") balance--;
    }
    depth = Math.max(0, depth + balance);
    return unit.repeat(indent) + body;
  });
  return formatted.join(newline);
}

module.exports = { formatIndentation };
