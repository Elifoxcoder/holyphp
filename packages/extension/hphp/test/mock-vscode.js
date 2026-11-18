/* test/mock-vscode.js — a small, shared `vscode` stand-in.
 *
 * Only the surface the extension touches; providers and commands are recorded
 * so tests can drive them. No DOM, no extension host.
 */
"use strict";

const Module = require("module");

function pos(line, character) {
  return {
    line, character,
    isBefore(o) { return line < o.line || (line === o.line && character < o.character); },
  };
}

class Position { constructor(line, character) { Object.assign(this, pos(line, character)); } }
class Range {
  constructor(a, b, c, d) {
    if (typeof a === "number") { this.start = new Position(a, b); this.end = new Position(c, d); }
    else { this.start = a; this.end = b; }
  }
}
class MarkdownString {
  constructor(v) { this.value = v || ""; }
  appendMarkdown(s) { this.value += s; return this; }
  appendCodeblock(s, l) { this.value += "```" + (l || "") + "\n" + s + "\n```"; return this; }
  appendText(s) { this.value += s; return this; }
}
class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
class SnippetString { constructor(v) { this.value = v; } }
class Diagnostic { constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; } }
class Hover { constructor(contents, range) { this.contents = contents; this.range = range; } }
class SignatureHelp { constructor() { this.signatures = []; this.activeSignature = 0; this.activeParameter = 0; } }
class SignatureInformation { constructor(label, doc) { this.label = label; this.documentation = doc; this.parameters = []; } }
class ParameterInformation { constructor(label, doc) { this.label = label; this.documentation = doc; } }
class Location { constructor(uri, rangeOrPos) { this.uri = uri; this.range = rangeOrPos; } }
class DocumentSymbol {
  constructor(name, detail, kind, range, selectionRange) {
    Object.assign(this, { name, detail, kind, range, selectionRange });
    this.children = [];
  }
}
class Uri {
  constructor(p) { this.scheme = "file"; this.fsPath = p; this.path = p; }
  static file(p) { return new Uri(p); }
  static parse(s) { return new Uri(String(s).replace(/^file:\/\//, "")); }
  toString() { return "file://" + this.fsPath; }
}

const registrations = { completion: [], hover: [], signature: [], symbols: [], definition: [] };
const commands = [];
const clients = [];
let statusBar = null;

const vscode = {
  Position, Range, MarkdownString, CompletionItem, SnippetString, Diagnostic,
  Hover, SignatureHelp, SignatureInformation, ParameterInformation, Location,
  DocumentSymbol, Uri,
  CompletionItemKind: {
    Text: 0, Method: 1, Function: 2, Field: 4, Variable: 5, Class: 6,
    Constant: 14, Keyword: 13, TypeParameter: 24,
  },
  DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
  SymbolKind: {
    Module: 1, Class: 4, Method: 5, Field: 7, Enum: 9, Interface: 10,
    Function: 11, Constant: 13, Object: 18,
  },
  StatusBarAlignment: { Left: 1, Right: 2 },
  languages: {
    registerCompletionItemProvider: (sel, p) => { registrations.completion.push(p); return { dispose() {} }; },
    registerHoverProvider: (sel, p) => { registrations.hover.push(p); return { dispose() {} }; },
    registerSignatureHelpProvider: (sel, p) => { registrations.signature.push(p); return { dispose() {} }; },
    registerDocumentSymbolProvider: (sel, p) => { registrations.symbols.push(p); return { dispose() {} }; },
    registerDefinitionProvider: (sel, p) => { registrations.definition.push(p); return { dispose() {} }; },
    createDiagnosticCollection: () => {
      const map = new Map();
      return {
        map,
        set(uri, diags) { map.set(uri.toString(), diags); },
        delete(uri) { map.delete(uri.toString()); },
        clear() { map.clear(); },
        get(uri) { return map.get(uri.toString()) || []; },
        dispose() {},
      };
    },
  },
  window: {
    activeTextEditor: undefined,
    createStatusBarItem: () => {
      statusBar = { text: "", tooltip: "", command: "", show() {}, hide() {}, dispose() {} };
      return statusBar;
    },
    createTerminal: () => ({ show() {}, sendText() {} }),
    showWarningMessage: () => Promise.resolve(undefined),
    onDidChangeActiveTextEditor: () => ({ dispose() {} }),
  },
  workspace: {
    textDocuments: [],
    getConfiguration: () => ({ get: (key, def) => def }),
    onDidChangeTextDocument: () => ({ dispose() {} }),
    onDidSaveTextDocument: () => ({ dispose() {} }),
    onDidOpenTextDocument: () => ({ dispose() {} }),
    onDidCloseTextDocument: () => ({ dispose() {} }),
    onDidChangeConfiguration: () => ({ dispose() {} }),
    createFileSystemWatcher: () => ({
      onDidChange: () => ({ dispose() {} }), onDidCreate: () => ({ dispose() {} }),
      onDidDelete: () => ({ dispose() {} }), dispose() {},
    }),
    findFiles: async () => [],
    fs: { readFile: async () => new Uint8Array() },
  },
  commands: {
    registerCommand: (id, fn) => { commands.push({ id, fn }); return { dispose() {} }; },
    executeCommand: () => Promise.resolve(),
  },
};

let installed = false;

/* Make require("vscode") return the stub. Idempotent. */
function install() {
  if (installed) return vscode;
  installed = true;
  const original = Module._load;
  Module._load = function (request, parent, isMain) {
    if (request === "vscode") return vscode;
    if (request === "vscode-languageclient/node") {
      class LanguageClient {
        constructor(id, name, serverOptions, clientOptions) { Object.assign(this, { id, name, serverOptions, clientOptions, requests: new Map() }); clients.push(this); }
        onRequest(method, handler) { this.requests.set(method, handler); }
        start() { this.started = true; return Promise.resolve(); }
        dispose() {}
      }
      return { LanguageClient, TransportKind: { stdio: 1 } };
    }
    return original.call(this, request, parent, isMain);
  };
  return vscode;
}

module.exports = { vscode, registrations, commands, clients, install, getStatusBar: () => statusBar, Uri, Range, Position };
