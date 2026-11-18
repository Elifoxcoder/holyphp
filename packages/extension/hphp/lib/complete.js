/* lib/complete.js — the completion model.
 *
 * Decides what the cursor is asking for (a variable, a member after `->`, a
 * type, a class after `new`, …) and produces plain item descriptors. The
 * extension maps those to vscode.CompletionItem; the tests use them directly.
 */
"use strict";

const data = require("./data");
const { linePrefix, findOpenParen } = require("./text");
const { enclosingClass, variableType } = require("./symbols");

/* The expression a `->` / `::` applies to (best effort). */
function receiverExpr(prefix) {
  const t = prefix.replace(/(?:\?->|->|::)\s*$/, "");
  const m = /(\$this|\$[A-Za-z_]\w*|[A-Za-z_]\w*)\s*$/.exec(t);
  return m ? m[1] : "";
}

/* Are we where a type belongs? Inside a function signature, after `let $x:`,
 * after `const X:`, after extends/implements. */
function isTypePosition(text, offset) {
  const open = findOpenParen(text, offset);
  if (open >= 0) {
    const before = text.slice(0, open);
    if (/\b(?:function|fn)\s+[A-Za-z_]\w*\s*$/.test(before) || /\bfn\s*$/.test(before)) return true;
  }
  const lp = linePrefix(text, offset);
  if (/\blet\s+\$[A-Za-z_]\w*\s*:\s*[A-Za-z_]*$/.test(lp)) return true;
  if (/\bconst\s+[A-Za-z_]\w*\s*:\s*[A-Za-z_]*$/.test(lp)) return true;
  if (/\b(?:implements|extends)\s+[A-Za-z_, ]*$/.test(lp)) return true;
  return false;
}

/* {kind, prefix, receiver} */
function completionContext(text, offset) {
  const lp = linePrefix(text, offset);
  const word = (/[A-Za-z0-9_$]*$/.exec(lp) || [""])[0];
  const before = lp.slice(0, lp.length - word.length);

  if (/(?:\?->|->)\s*$/.test(before)) {
    return { kind: "member", prefix: word, receiver: receiverExpr(before) };
  }
  if (/::\s*$/.test(before)) {
    return { kind: "static", prefix: word, receiver: receiverExpr(before) };
  }
  if (/\bnew\s+$/.test(before)) return { kind: "class", prefix: word };
  if (isTypePosition(text, offset)) return { kind: "type", prefix: word };
  if (/\bfunction\s+$/.test(before)) return { kind: "function-name", prefix: word };
  if (/\b(?:let|const|global)\s+\$?$/.test(before)) return { kind: "declare", prefix: word };
  return { kind: "general", prefix: word };
}

/* Members of `className`, walking `extends` inside the file. */
function classMembers(symbols, className, seen) {
  const name = (className || "").replace(/^\\/, "");
  if (!name) return [];
  const visited = seen || new Set();
  if (visited.has(name)) return [];
  visited.add(name);
  const cls = symbols.classes.find((c) => c.name === name);
  if (!cls) return [];
  const out = [];
  for (const m of cls.methods) out.push({ ...m, kind: "method", owner: cls.name });
  for (const f of cls.fields) out.push({ ...f, kind: "field", owner: cls.name });
  for (const c of cls.constants) out.push({ ...c, kind: "constant", owner: cls.name });
  if (cls.extends) out.push(...classMembers(symbols, cls.extends, visited));
  return out;
}

/* The class a receiver expression refers to, or "". */
function resolveReceiver(symbols, text, receiver, offset) {
  if (!receiver) return "";
  if (receiver === "$this") {
    const c = enclosingClass(symbols, offset);
    return c ? c.name : "";
  }
  if (receiver[0] === "$") return variableType(symbols, text, receiver.slice(1), offset);
  return receiver;
}

function docBlock(code) {
  return "```holyphp\n" + code + "\n```";
}

function builtinItem(b) {
  if (b.constant) {
    const c = data.constant(b.name);
    return {
      label: b.name,
      kind: "constant",
      detail: `${b.name} = ${c ? c.literal : "?"} · ${c ? c.type : "int"}`,
      documentation: (c ? c.desc : "") + "\n\n" + docBlock(`${b.name} = ${c ? c.literal : "?"}`),
      insertText: b.name,
      snippet: false,
      sortText: "3" + b.name,
    };
  }
  const insertParams = b.params.filter((p) => !p.optional);
  const snippet = insertParams.length
    ? b.name + "(" + insertParams.map((p, i) =>
        "${" + (i + 1) + ":" + (p.name || ("arg" + (i + 1))) + "}").join(", ") + ")$0"
    : b.name + "()$0";
  return {
    label: b.name,
    kind: "function",
    detail: b.sig + (b.unsafe ? "  (unsafe)" : ""),
    documentation: (b.desc || "builtin") +
      (b.unsafe ? "\n\n⚠️ only callable inside `unsafe` blocks" : "") +
      "\n\n" + docBlock(b.sig),
    insertText: snippet,
    snippet: insertParams.length > 0,
    sortText: "2" + b.name,
  };
}

function functionItem(f) {
  const snippet = f.params.length
    ? f.name + "(" + f.params.map((p, i) =>
        "${" + (i + 1) + ":" + p.name + "}").join(", ") + ")$0"
    : f.name + "()$0";
  return {
    label: f.name,
    kind: "function",
    detail: `function (${f.paramsRaw || ""}): ${f.ret}`,
    documentation: docBlock(`function ${f.name}(${f.paramsRaw || ""}): ${f.ret}`),
    insertText: snippet,
    snippet: f.params.length > 0,
    sortText: "1" + f.name,
  };
}

function memberItem(m) {
  if (m.kind === "method") {
    const params = m.params || [];
    const snippet = params.length
      ? m.name + "(" + params.map((p, i) => "${" + (i + 1) + ":" + p.name + "}").join(", ") + ")$0"
      : m.name + "()$0";
    return {
      label: m.name,
      kind: "method",
      detail: `${m.owner}.${m.name}(${(m.paramsRaw || "")}): ${m.ret}`,
      documentation: docBlock(`${m.owner}.${m.name}(${m.paramsRaw || ""}): ${m.ret}`),
      insertText: snippet,
      snippet: params.length > 0,
      sortText: "0" + m.name,
    };
  }
  if (m.kind === "field") {
    return {
      label: m.name,
      kind: "field",
      detail: `${m.owner}.$${m.name}: ${m.type}`,
      documentation: docBlock(`$${m.name}: ${m.type}`),
      insertText: m.name,
      snippet: false,
      sortText: "0" + m.name,
    };
  }
  return {
    label: m.name,
    kind: "constant",
    detail: `${m.owner}::${m.name}`,
    documentation: docBlock(`${m.owner}::${m.name}`),
    insertText: m.name,
    snippet: false,
    sortText: "0" + m.name,
  };
}

function variableItem(v) {
  const type = v.type ? ": " + v.type : "";
  return {
    label: "$" + v.name,
    kind: "variable",
    detail: "$" + v.name + type,
    documentation: v.declared ? `Declared on line ${v.line + 1}` : undefined,
    insertText: "$" + v.name,
    snippet: false,
    sortText: "0" + v.name,
  };
}

/* Build the item list for a context. */
function buildItems(ctx, symbols, text, offset, model) {
  const d = model || data.load();
  const items = [];

  if (ctx.kind === "member" || ctx.kind === "static") {
    const type = resolveReceiver(symbols, text, ctx.receiver, offset);
    const members = type
      ? classMembers(symbols, type)
      : symbols.classes.flatMap((c) => classMembers(symbols, c.name));
    const seen = new Set();
    for (const m of members) {
      if (seen.has(m.kind + m.name)) continue;
      seen.add(m.kind + m.name);
      items.push(memberItem(m));
    }
    return items;
  }

  if (ctx.kind === "class") {
    for (const c of symbols.classes) {
      items.push({
        label: c.name,
        kind: "class",
        detail: `${c.kind} ${c.name}${c.extends ? " extends " + c.extends : ""}`,
        documentation: docBlock(`${c.kind} ${c.name}`),
        insertText: c.name,
        snippet: false,
        sortText: "0" + c.name,
      });
    }
    return items;
  }

  if (ctx.kind === "type") {
    for (const t of data.TYPES) {
      items.push({
        label: t,
        kind: "type",
        detail: "HolyPHP type",
        insertText: t,
        snippet: false,
        sortText: "0" + t,
      });
    }
    for (const c of symbols.classes) {
      items.push({
        label: c.name,
        kind: "class",
        detail: `${c.kind} ${c.name}`,
        insertText: c.name,
        snippet: false,
        sortText: "0" + c.name,
      });
    }
    return items;
  }

  if (ctx.kind === "function-name") {
    return items;
  }

  if (ctx.kind === "declare") {
    for (const t of data.TYPES) {
      items.push({
        label: t,
        kind: "type",
        detail: "HolyPHP type",
        insertText: t,
        snippet: false,
        sortText: "0" + t,
      });
    }
    return items;
  }

  /* general: variables, user declarations, builtins, keywords, types */
  for (const v of symbols.variables) items.push(variableItem(v));
  for (const f of symbols.functions) items.push(functionItem(f));
  for (const c of symbols.classes) {
    items.push({
      label: c.name,
      kind: "class",
      detail: `${c.kind} ${c.name}`,
      insertText: c.name,
      snippet: false,
      sortText: "1" + c.name,
    });
  }
  for (const c of symbols.constants) {
    items.push({
      label: c.name,
      kind: "constant",
      detail: "const " + c.name,
      insertText: c.name,
      snippet: false,
      sortText: "1" + c.name,
    });
  }
  for (const b of d.builtins) items.push(builtinItem(b));
  for (const [w, kind, detail] of data.KEYWORDS) {
    items.push({
      label: w,
      kind: "keyword",
      detail: "HolyPHP " + (kind === "statement" ? "statement" : "keyword"),
      documentation: docBlock(detail),
      insertText: w,
      snippet: false,
      sortText: "3" + w,
    });
  }
  for (const t of data.TYPES) {
    items.push({
      label: t,
      kind: "type",
      detail: "HolyPHP type",
      insertText: t,
      snippet: false,
      sortText: "4" + t,
    });
  }
  return items;
}

module.exports = {
  completionContext,
  buildItems,
  classMembers,
  resolveReceiver,
  isTypePosition,
  receiverExpr,
};
