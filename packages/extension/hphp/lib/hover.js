/* lib/hover.js — markdown for the symbol under the cursor.
 *
 * Pure: returns {word, markdown} or null. The extension wraps it in
 * vscode.Hover with the word's range.
 */
"use strict";

const data = require("./data");
const { wordRangeAt } = require("./text");
const { collectSymbols, enclosingClass, variableType } = require("./symbols");

function code(s) {
  return "```holyphp\n" + s + "\n```";
}

function describeVariable(text, symbols, name) {
  const type = variableType(symbols, text, name);
  const uses = (text.match(new RegExp("\\$" + name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + "\\b", "g")) || []).length;
  const v = symbols.variables.find((x) => x.name === name);
  const bits = [];
  if (v) bits.push("declared line " + (v.line + 1));
  bits.push("used " + uses + "×");
  if (!type) bits.push("type not inferred");
  return {
    word: "$" + name,
    markdown:
      "**$" + name + "** — variable\n\n" +
      code("$" + name + (type ? ": " + type : "")) +
      "\n\n" + bits.join(" · ") +
      "\n\n_Static view — runtime values only show while the program runs._",
  };
}

function hoverFor(text, offset, symbols) {
  const range = wordRangeAt(text, offset);
  const word = range.word;
  if (!word) return null;

  if (word[0] === "$") {
    return describeVariable(text, symbols, word.slice(1));
  }

  const c = data.constant(word);
  if (c) {
    return {
      word,
      markdown:
        "**" + word + "** — predefined constant (" + c.type + ")\n\n" +
        code(word + " = " + c.literal) +
        "\n\n" + c.desc,
    };
  }

  const b = data.builtin(word);
  if (b) {
    let md = "**" + word + "** — builtin\n\n" + code(b.sig) + "\n\n" + (b.desc || "");
    if (b.params && b.params.length) {
      md += "\n\n**Parameters**\n\n" + b.params.map((p) =>
        `- \`${p.name}\` (\`${p.type}\`): ${p.description || "No additional parameter notes."}`
      ).join("\n");
    }
    if (b.returns) md += "\n\n**Returns:** " + b.returns;
    if (b.example) md += "\n\n**Example**\n\n" + code(b.example);
    if (b.exampleOutput) md += "\n\n_Output:_\n\n" + code(b.exampleOutput);
    if (b.seeAlso && b.seeAlso.length) md += "\n\n**See also:** " + b.seeAlso.map((x) => `\`${x}()\``).join(", ");
    if (b.authoredNoCode) md += "\n\n_This builtin has no standalone example because it is interactive, blocking, unsafe, or has external side effects._";
    if (b.unsafe) md += "\n\n⚠️ **unsafe**: only callable inside `unsafe` blocks";
    return { word, markdown: md };
  }

  const fn = symbols.functions.find((f) => f.name === word);
  if (fn) {
    return {
      word,
      markdown:
        "**" + word + "** — function\n\n" +
        code("function " + fn.name + "(" + (fn.paramsRaw || "") + "): " + fn.ret) +
        "\n\nDeclared on line " + (fn.line + 1),
    };
  }

  const cls = symbols.classes.find((k) => k.name === word);
  if (cls) {
    const members = [
      ...cls.methods.map((m) => "  function " + m.name + "(" + (m.paramsRaw || "") + "): " + m.ret),
      ...cls.fields.map((f) => "  " + f.type + " $" + f.name),
    ].join("\n");
    return {
      word,
      markdown:
        "**" + word + "** — " + cls.kind + "\n\n" +
        code(cls.kind + " " + cls.name + (cls.extends ? " extends " + cls.extends : "") +
          (members ? " {\n" + members + "\n}" : "")) +
        "\n\nDeclared on line " + (cls.line + 1),
    };
  }

  const kw = data.KEYWORDS.find((k) => k[0] === word);
  if (kw) {
    return {
      word,
      markdown: "**" + word + "** — " + kw[1] + "\n\n" + code(kw[2]),
    };
  }

  if (data.TYPES.includes(word)) {
    return { word, markdown: "**" + word + "** — type" };
  }

  const enc = enclosingClass(symbols, offset);
  if (enc) {
    const m = enc.methods.find((x) => x.name === word) || enc.fields.find((x) => x.name === word);
    if (m) {
      const sig = m.paramsRaw !== undefined
        ? "function " + m.name + "(" + m.paramsRaw + "): " + m.ret
        : m.type + " $" + m.name;
      return {
        word,
        markdown:
          "**" + word + "** — member of " + enc.name + "\n\n" + code(sig) +
          "\n\nDeclared on line " + (m.line + 1),
      };
    }
  }

  return null;
}

module.exports = { hoverFor };
