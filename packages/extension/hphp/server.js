/* HolyPHP Language Server (LSP 3.17). Kept dependency-light and based on the
 * same pure language model used by the extension. */
"use strict";
const { createConnection, ProposedFeatures, TextDocuments, CompletionItemKind, SymbolKind, CodeActionKind } = require("vscode-languageserver/node");
const { TextDocument } = require("vscode-languageserver-textdocument");
const data = require("./lib/data");
const { collectSymbols, variableType, enclosingClass } = require("./lib/symbols");
const { stripNonCode } = require("./lib/text");
const { completionContext, buildItems } = require("./lib/complete");
const { hoverFor } = require("./lib/hover");
const { signatureAt } = require("./lib/signature");
const { formatIndentation } = require("./lib/format");

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const kindMap = { variable: CompletionItemKind.Variable, function: CompletionItemKind.Function, method: CompletionItemKind.Method, field: CompletionItemKind.Field, class: CompletionItemKind.Class, constant: CompletionItemKind.Constant, keyword: CompletionItemKind.Keyword, type: CompletionItemKind.TypeParameter };
connection.onInitialize(() => ({
  capabilities: {
    textDocumentSync: { openClose: true, change: 2, save: { includeText: false } },
    completionProvider: { triggerCharacters: ["$", ">", ":", "\"", "'", "/"], resolveProvider: false },
    hoverProvider: true,
    signatureHelpProvider: { triggerCharacters: ["(", ","], retriggerCharacters: [","] },
    documentSymbolProvider: true,
    workspaceSymbolProvider: true,
    definitionProvider: true,
    referencesProvider: true,
    renameProvider: { prepareProvider: true },
    codeActionProvider: { codeActionKinds: [CodeActionKind.QuickFix] },
    documentFormattingProvider: true,
  },
  serverInfo: { name: "HolyPHP Language Server", version: "0.3.0" },
}));
connection.onDocumentFormatting(({ textDocument, options }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return [];
  const formatted = formatIndentation(doc.getText(), options.tabSize, options.insertSpaces);
  if (formatted === doc.getText()) return [];
  return [{ range: { start: { line: 0, character: 0 }, end: doc.positionAt(doc.getText().length) }, newText: formatted }];
});
function editDistance(a, b) {
  const row = Array.from({ length: b.length + 1 }, (_, i) => i);
  for (let i = 1; i <= a.length; i++) {
    let prev = row[0]; row[0] = i;
    for (let j = 1; j <= b.length; j++) {
      const old = row[j]; row[j] = Math.min(row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] === b[j - 1] ? 0 : 1)); prev = old;
    }
  }
  return row[b.length];
}
connection.onCodeAction(({ textDocument, context }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return [];
  const local = collectSymbols(doc.getText());
  const knownFunctions = [...data.load().builtins.filter((b) => !b.constant).map((b) => b.name), ...local.functions.map((f) => f.name)];
  const knownVariables = local.variables.map((v) => "$" + v.name);
  const knownTypes = local.classes.map((c) => c.name);
  const actions = [];
  for (const diagnostic of context.diagnostics || []) {
    const candidates = [];
    let m = /undefined function ['\"]?([A-Za-z_]\w*)\(\)/i.exec(diagnostic.message);
    if (m) candidates.push({ bad: m[1], known: knownFunctions });
    m = /undefined variable \$([A-Za-z_]\w*)/i.exec(diagnostic.message);
    if (m) candidates.push({ bad: "$" + m[1], known: knownVariables });
    m = /unknown type ['\"]?([A-Za-z_]\w*)/i.exec(diagnostic.message);
    if (m) candidates.push({ bad: m[1], known: knownTypes });
    for (const { bad, known } of candidates) {
      const ranked = [...new Set(known)].map((name) => ({ name, score: editDistance(bad.toLowerCase(), name.toLowerCase()) }))
        .filter((item) => item.score > 0 && item.score <= Math.min(3, Math.max(1, Math.floor(bad.length / 2))))
        .sort((a, b) => a.score - b.score || a.name.localeCompare(b.name));
      for (const suggestion of ranked.slice(0, 3)) actions.push({ title: `Replace with '${suggestion.name}'`, kind: CodeActionKind.QuickFix,
        diagnostics: [diagnostic], edit: { changes: { [doc.uri]: [{ range: diagnostic.range, newText: suggestion.name }] } } });
    }
  }
  return actions;
});
connection.onCompletion(async (params) => {
  const doc = documents.get(params.textDocument.uri); if (!doc) return [];
  const text = doc.getText(), offset = doc.offsetAt(params.position), symbols = collectSymbols(text);
  const importPrefix = /\bimport\s+["']([^"']*)$/.exec(text.slice(0, offset));
  if (importPrefix) {
    const names = await connection.sendRequest("holyphp/workspaceImports", { uri: doc.uri, prefix: importPrefix[1] });
    const start = doc.positionAt(offset - importPrefix[1].length);
    return (names || []).map((name) => ({ label: name, kind: CompletionItemKind.File, detail: "HolyPHP project module",
      textEdit: { range: { start, end: params.position }, newText: name }, insertText: name, sortText: name }));
  }
  return buildItems(completionContext(text, offset), symbols, text, offset).map((it) => ({
    label: it.label, kind: kindMap[it.kind] || CompletionItemKind.Text,
    detail: it.detail, documentation: it.documentation ? { kind: "markdown", value: it.documentation } : undefined,
    insertText: it.insertText, insertTextFormat: it.snippet ? 2 : 1, sortText: it.sortText,
  }));
});
connection.onHover((params) => {
  const doc = documents.get(params.textDocument.uri); if (!doc) return null;
  const h = hoverFor(doc.getText(), doc.offsetAt(params.position), collectSymbols(doc.getText()));
  return h ? { contents: { kind: "markdown", value: h.markdown } } : null;
});
connection.onSignatureHelp((params) => {
  const doc = documents.get(params.textDocument.uri); if (!doc) return null;
  const text = doc.getText(), call = signatureAt(text, doc.offsetAt(params.position)); if (!call) return null;
  const b = data.builtin(call.name), fn = collectSymbols(text).functions.find((f) => f.name === call.name);
  if (!b && !fn) return null;
  const label = b ? b.sig : `function ${fn.name}(${fn.paramsRaw || ""}): ${fn.ret}`;
  const paramsInfo = b ? b.params : fn.params;
  return { signatures: [{ label, documentation: b && b.desc, parameters: paramsInfo.map((p) => ({ label: p.name + (p.type ? `: ${p.type}` : ""), documentation: b && b.params && (b.params.find((x) => x.name === p.name) || {}).description })) }], activeSignature: 0, activeParameter: Math.min(call.active, Math.max(0, paramsInfo.length - 1)) };
});
function symbolsFor(doc) {
  const s = collectSymbols(doc.getText()), result = [];
  for (const c of s.classes) {
    result.push({ name: c.name, detail: c.kind, kind: c.kind === "enum" ? SymbolKind.Enum : SymbolKind.Class, line: c.line, nameOffset: c.nameOffset });
    for (const m of c.methods) result.push({ name: m.name, detail: `${c.name}::${m.name}(${m.paramsRaw || ""}): ${m.ret}`, kind: SymbolKind.Method, line: m.line, nameOffset: m.nameOffset, owner: c.name });
    for (const f of c.fields) result.push({ name: f.name, detail: `${c.name}.$${f.name}: ${f.type}`, kind: SymbolKind.Field, line: f.line, nameOffset: f.nameOffset, owner: c.name });
    for (const k of c.constants) result.push({ name: k.name, detail: `${c.name}::${k.name}`, kind: SymbolKind.Constant, line: k.line, nameOffset: k.nameOffset, owner: c.name });
  }
  for (const f of s.functions) result.push({ name: f.name, detail: `(${f.paramsRaw || ""}): ${f.ret}`, kind: SymbolKind.Function, line: f.line, nameOffset: f.nameOffset });
  for (const c of s.constants) result.push({ name: c.name, detail: "constant", kind: SymbolKind.Constant, line: c.line, nameOffset: c.nameOffset });
  return result;
}
function rangeAt(doc, offset, length) {
  const start = doc.positionAt(offset);
  return { start, end: doc.positionAt(offset + length) };
}
function wordAt(doc, position) {
  const text = doc.getText(), offset = doc.offsetAt(position);
  const re = /[A-Za-z_]\w*/g;
  let m;
  while ((m = re.exec(text))) if (offset >= m.index && offset <= m.index + m[0].length) return { name: m[0], start: m.index, end: m.index + m[0].length };
  return null;
}
connection.onDocumentSymbol(({ textDocument }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return [];
  return symbolsFor(doc).filter((s) => !s.owner).map((s) => ({ name: s.name, detail: s.detail, kind: s.kind, range: rangeAt(doc, s.nameOffset, s.name.length), selectionRange: rangeAt(doc, s.nameOffset, s.name.length) }));
});
connection.onWorkspaceSymbol(async ({ query }) => {
  const out = [];
  const openUris = new Set(documents.all().map((doc) => doc.uri));
  for (const doc of documents.all()) {
    for (const s of symbolsFor(doc)) {
      if (!query || s.name.toLowerCase().includes(query.toLowerCase())) {
        out.push({ name: s.name, kind: s.kind, containerName: s.detail, location: { uri: doc.uri, range: { start: { line: s.line, character: 0 }, end: { line: s.line, character: s.name.length } } } });
      }
    }
  }
  const closed = await connection.sendRequest("holyphp/findWorkspaceSymbols", { query: query || "", excludeUris: [...openUris] });
  return out.concat(closed || []);
});
connection.onDefinition(async ({ textDocument, position }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return null;
  const word = wordAt(doc, position); if (!word) return null;
  const text = doc.getText(), before = text.slice(0, word.start);
  const member = /(\$[A-Za-z_]\w*|[A-Za-z_]\w*)\s*(->|::)\s*$/.exec(before);
  let owner = "";
  if (member) {
    if (member[2] === "::") {
      owner = member[1].replace(/^\$/, "");
      if (owner === "self" || owner === "static") owner = (enclosingClass(collectSymbols(text), word.start) || {}).name || owner;
    } else if (member[1] === "$this") owner = (enclosingClass(collectSymbols(text), word.start) || {}).name || "";
    else if (member[1][0] === "$") owner = variableType(collectSymbols(text), text, member[1].slice(1), word.start);
  }
  const candidates = [];
  for (const candidateDoc of documents.all()) {
    for (const symbol of symbolsFor(candidateDoc)) {
      if (symbol.name !== word.name) continue;
      if (owner && symbol.owner !== owner && symbol.kind !== SymbolKind.Class) continue;
      candidates.push({ doc: candidateDoc, symbol });
    }
  }
  const found = candidates[0];
  if (!found) {
    const external = await connection.sendRequest("holyphp/findDefinition", { name: word.name, owner });
    return external && external.length ? external : null;
  }
  return { uri: found.doc.uri, range: rangeAt(found.doc, found.symbol.nameOffset, found.symbol.name.length) };
});
connection.onReferences(async ({ textDocument, position, context }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return [];
  const word = wordAt(doc, position); if (!word) return [];
  const found = [];
  for (const candidateDoc of documents.all()) {
    const text = candidateDoc.getText();
    const code = stripNonCode(text);
    const re = new RegExp(`\\b${word.name}\\b`, "g");
    let m;
    while ((m = re.exec(code))) {
      if (!context.includeDeclaration) {
        const declared = symbolsFor(candidateDoc).some((s) => s.name === word.name && s.nameOffset === m.index);
        if (declared) continue;
      }
      found.push({ uri: candidateDoc.uri, range: rangeAt(candidateDoc, m.index, word.name.length) });
    }
  }
  const openUris = documents.all().map((candidateDoc) => candidateDoc.uri);
  const external = await connection.sendRequest("holyphp/findReferences", { name: word.name, excludeUris: openUris });
  return found.concat(external || []);
});
connection.onPrepareRename(async ({ textDocument, position }) => {
  const doc = documents.get(textDocument.uri); if (!doc) return null;
  const word = wordAt(doc, position); if (!word) return null;
  if (data.builtin(word.name)) return null;
  const declared = documents.all().some((candidateDoc) => symbolsFor(candidateDoc).some((symbol) => symbol.name === word.name) || collectSymbols(candidateDoc.getText()).variables.some((v) => v.name === word.name));
  if (!declared) {
    const external = await connection.sendRequest("holyphp/findDefinition", { name: word.name, owner: "" });
    if (!external || !external.length) return null;
  }
  return { range: rangeAt(doc, word.start, word.name.length), placeholder: word.name };
});
connection.onRenameRequest(async ({ textDocument, position, newName }) => {
  if (!/^[A-Za-z_]\w*$/.test(newName || "")) return null;
  const doc = documents.get(textDocument.uri); if (!doc) return null;
  const word = wordAt(doc, position); if (!word) return null;
  if (data.builtin(word.name)) return null;
  const changes = {};
  for (const candidateDoc of documents.all()) {
    const text = candidateDoc.getText(), code = stripNonCode(text), re = new RegExp(`\\b${word.name}\\b`, "g"), edits = [];
    let m;
    while ((m = re.exec(code))) edits.push({ range: rangeAt(candidateDoc, m.index, word.name.length), newText: newName });
    if (edits.length) changes[candidateDoc.uri] = edits;
  }
  const external = await connection.sendRequest("holyphp/rename", { name: word.name, newName, excludeUris: documents.all().map((candidateDoc) => candidateDoc.uri) });
  Object.assign(changes, external || {});
  return { changes };
});
documents.listen(connection);
connection.listen();
