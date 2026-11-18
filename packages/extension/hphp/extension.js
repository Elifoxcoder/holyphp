/* extension.js — HolyPHP language support for VS Code.
 *
 * The editor glue lives here; everything testable is in lib/:
 *   lib/data.js        builtins (generated), constants, keywords, signatures
 *   lib/text.js        offsets, word ranges, comment/string stripping
 *   lib/symbols.js     functions, classes, methods, fields, variables
 *   lib/diagnostics.js `hphp check` output -> line/column/severity
 *   lib/complete.js    what to offer where (member, type, class, general)
 *   lib/hover.js       markdown for the symbol under the cursor
 *   lib/signature.js   the call the cursor is in and its active argument
 *
 * Live diagnostics run `hphp check` on the *buffer*: a dirty document is
 * written to a temporary file next to the real one (so relative imports still
 * resolve), checked, and the temporary file is removed again. Syntax errors
 * (the compiler's `hphpc: error: file:line:col:` style) are mapped just like
 * sema errors.
 */
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const os = require("os");
const { execFile } = require("child_process");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

const data = require("./lib/data");
const { collectSymbols } = require("./lib/symbols");
const { parseCompilerOutput, rangeFor, samePath } = require("./lib/diagnostics");
const { completionContext, buildItems } = require("./lib/complete");
const { hoverFor } = require("./lib/hover");
const { signatureAt } = require("./lib/signature");
const { stripNonCode } = require("./lib/text");

const SELECTOR = [{ language: "holyphp" }];
const KIND = {
  variable: vscode.CompletionItemKind.Variable,
  function: vscode.CompletionItemKind.Function,
  method: vscode.CompletionItemKind.Method,
  field: vscode.CompletionItemKind.Field,
  class: vscode.CompletionItemKind.Class,
  constant: vscode.CompletionItemKind.Constant,
  keyword: vscode.CompletionItemKind.Keyword,
  type: vscode.CompletionItemKind.TypeParameter,
};

function cfg() {
  return vscode.workspace.getConfiguration("holyphp");
}

/* ---------------- completion ---------------- */

function toItem(it) {
  const item = new vscode.CompletionItem(it.label, KIND[it.kind] || vscode.CompletionItemKind.Text);
  item.detail = it.detail;
  if (it.documentation) item.documentation = new vscode.MarkdownString(it.documentation);
  item.insertText = it.snippet ? new vscode.SnippetString(it.insertText) : it.insertText;
  item.sortText = it.sortText;
  return item;
}

function registerCompletion(ctx) {
  ctx.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      SELECTOR,
      {
        provideCompletionItems(doc, pos) {
          const text = doc.getText();
          const offset = doc.offsetAt(pos);
          const symbols = collectSymbols(text);
          const context = completionContext(text, offset);
          return buildItems(context, symbols, text, offset).map(toItem);
        },
      },
      "$", ">", ":"
    )
  );
}

/* ---------------- hover ---------------- */

function registerHover(ctx) {
  ctx.subscriptions.push(
    vscode.languages.registerHoverProvider(SELECTOR, {
      provideHover(doc, pos) {
        const text = doc.getText();
        const symbols = collectSymbols(text);
        const h = hoverFor(text, doc.offsetAt(pos), symbols);
        if (!h) return null;
        const range = doc.getWordRangeAtPosition(pos, /[$A-Za-z_][A-Za-z0-9_]*/);
        return new vscode.Hover(new vscode.MarkdownString(h.markdown), range);
      },
    })
  );
}

/* ---------------- signature help ---------------- */

function registerSignatureHelp(ctx) {
  ctx.subscriptions.push(
    vscode.languages.registerSignatureHelpProvider(
      SELECTOR,
      {
        provideSignatureHelp(doc, pos) {
          const text = doc.getText();
          const sig = signatureAt(text, doc.offsetAt(pos));
          if (!sig) return null;

          const symbols = collectSymbols(text);
          const fn = symbols.functions.find((f) => f.name === sig.name);
          const b = data.builtin(sig.name);
          let label, params, docMd;
          if (fn) {
            label = `${fn.name}(${fn.paramsRaw || ""}): ${fn.ret}`;
            params = fn.params;
          } else if (b) {
            label = b.sig;
            params = b.params;
            docMd = b.desc || "";
            if (b.unsafe) docMd += "\n\n⚠️ only callable inside `unsafe` blocks";
          } else {
            return null;
          }

          const info = new vscode.SignatureInformation(label,
            docMd ? new vscode.MarkdownString(docMd) : undefined);
          info.parameters = params.map((p) => new vscode.ParameterInformation(p.name, p.type));
          const help = new vscode.SignatureHelp();
          help.signatures = [info];
          help.activeSignature = 0;
          help.activeParameter = Math.max(0, Math.min(sig.active, params.length - 1));
          return help;
        },
      },
      "(", ","
    )
  );
}

/* ---------------- document symbols ---------------- */

function symbolKind(kind) {
  switch (kind) {
    case "class": return vscode.SymbolKind.Class;
    case "interface": return vscode.SymbolKind.Interface;
    case "trait": return vscode.SymbolKind.Class;
    case "enum": return vscode.SymbolKind.Enum;
    default: return vscode.SymbolKind.Object;
  }
}

function lineRange(doc, line) {
  const l = Math.max(0, Math.min(line, doc.lineCount - 1));
  return doc.lineAt(l).range;
}

function registerSymbols(ctx) {
  ctx.subscriptions.push(
    vscode.languages.registerDocumentSymbolProvider(SELECTOR, {
      provideDocumentSymbols(doc) {
        const text = doc.getText();
        const s = collectSymbols(text);
        const out = [];
        for (const c of s.classes) {
          const sel = lineRange(doc, c.line);
          const end = doc.positionAt(Math.min(c.endOffset, text.length));
          const sym = new vscode.DocumentSymbol(
            c.name, c.kind, symbolKind(c.kind),
            new vscode.Range(sel.start, end), sel
          );
          for (const m of c.methods) {
            const r = lineRange(doc, m.line);
            sym.children.push(new vscode.DocumentSymbol(
              m.name, `(${m.paramsRaw || ""}): ${m.ret}`, vscode.SymbolKind.Method, r, r));
          }
          for (const f of c.fields) {
            const r = lineRange(doc, f.line);
            sym.children.push(new vscode.DocumentSymbol(
              f.name, f.type, vscode.SymbolKind.Field, r, r));
          }
          for (const k of c.constants) {
            const r = lineRange(doc, k.line);
            sym.children.push(new vscode.DocumentSymbol(
              k.name, "const", vscode.SymbolKind.Constant, r, r));
          }
          out.push(sym);
        }
        for (const f of s.functions) {
          const r = lineRange(doc, f.line);
          out.push(new vscode.DocumentSymbol(
            f.name, `(${f.paramsRaw || ""}): ${f.ret}`, vscode.SymbolKind.Function, r, r));
        }
        for (const c of s.constants) {
          const r = lineRange(doc, c.line);
          out.push(new vscode.DocumentSymbol(c.name, "const", vscode.SymbolKind.Constant, r, r));
        }
        for (const i of s.imports) {
          const r = lineRange(doc, i.line);
          out.push(new vscode.DocumentSymbol(i.name, "import", vscode.SymbolKind.Module, r, r));
        }
        return out;
      },
    })
  );
}

/* ---------------- go to definition (same file) ---------------- */

function registerDefinition(ctx) {
  ctx.subscriptions.push(
    vscode.languages.registerDefinitionProvider(SELECTOR, {
      provideDefinition(doc, pos) {
        const range = doc.getWordRangeAtPosition(pos, /[$A-Za-z_][A-Za-z0-9_]*/);
        if (!range) return null;
        const word = doc.getText(range).replace(/^\$/, "");
        const s = collectSymbols(doc.getText());
        const fn = s.functions.find((f) => f.name === word);
        const cls = s.classes.find((c) => c.name === word);
        const target = fn || cls;
        if (!target) return null;
        return new vscode.Location(doc.uri, new vscode.Position(target.line, 0));
      },
    })
  );
}

/* Search the current file and open workspace files for HolyPHP declarations. */
function registerWorkspaceSymbols(ctx) {
  ctx.subscriptions.push(vscode.languages.registerWorkspaceSymbolProvider({
    provideWorkspaceSymbols(query) {
      const out = [];
      for (const doc of vscode.workspace.textDocuments) {
        if (doc.languageId !== "holyphp") continue;
        const symbols = collectSymbols(doc.getText());
        for (const item of [...symbols.functions, ...symbols.classes, ...symbols.constants]) {
          if (query && !item.name.toLowerCase().includes(query.toLowerCase())) continue;
          const kind = item.kind === "class" || item.kind === "interface" || item.kind === "trait" || item.kind === "enum"
            ? vscode.SymbolKind.Class : item.params ? vscode.SymbolKind.Function : vscode.SymbolKind.Constant;
          out.push(new vscode.SymbolInformation(item.name, kind, "", new vscode.Location(doc.uri, new vscode.Position(item.line, 0))));
        }
      }
      return out;
    },
  }));
}

/* A searchable reference view backed by the same per-function docs as hover. */
function registerBuiltinReference(ctx) {
  ctx.subscriptions.push(vscode.commands.registerCommand("holyphp.builtins", async () => {
    const all = data.load().builtins;
    const pick = await vscode.window.showQuickPick(all.map((b) => ({
      label: b.name,
      description: b.sig,
      detail: b.desc,
      builtin: b,
    })), { placeHolder: "Search 300+ HolyPHP builtins by name or description", matchOnDescription: true, matchOnDetail: true });
    if (!pick) return;
    const b = pick.builtin;
    const params = (b.params || []).map((p) => `- \`${p.name}\` (**${p.type}**): ${p.description || "—"}`).join("\n");
    let md = `# \`${b.name}()\`\n\n\`\`\`holyphp\n${b.sig}\n\`\`\`\n\n${b.desc || ""}`;
    if (params) md += `\n\n## Parameters\n\n${params}`;
    if (b.returns) md += `\n\n## Return value\n\n${b.returns}`;
    if (b.example) md += `\n\n## Example\n\n\`\`\`holyphp\n${b.example}\n\`\`\``;
    if (b.exampleOutput) md += `\n\nOutput:\n\n\`\`\`text\n${b.exampleOutput}\n\`\`\``;
    if (b.seeAlso && b.seeAlso.length) md += `\n\nSee also: ${b.seeAlso.map((n) => `\`${n}()\``).join(", ")}`;
    if (b.authoredNoCode) md += "\n\n> This builtin is interactive, blocking, unsafe, or has external side effects, so its reference intentionally omits a runnable sample.";
    if (b.unsafe) md += "\n\n> Requires an `unsafe { ... }` block.";
    const document = await vscode.workspace.openTextDocument({ language: "markdown", content: md });
    await vscode.window.showTextDocument(document, { preview: true });
  }));
}

let workspaceIndexPromise = null;
function positionAt(text, offset, starts) {
  let lo = 0, hi = starts.length - 1;
  while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (starts[mid] <= offset) lo = mid; else hi = mid - 1; }
  return { line: lo, character: offset - starts[lo] };
}
function lineStartsFor(text) {
  const starts = [0];
  for (let i = 0; i < text.length; i++) if (text.charCodeAt(i) === 10) starts.push(i + 1);
  return starts;
}
function buildWorkspaceIndex() {
  if (workspaceIndexPromise) return workspaceIndexPromise;
  workspaceIndexPromise = (async () => {
    const limit = Math.max(100, Math.min(20000, cfg().get("maxIndexedFiles", 5000)));
    const files = await vscode.workspace.findFiles("**/*.hphp", "**/{node_modules,.git,build,dist,target}/**", limit);
    const entries = [];
    let cursor = 0;
    const workers = Array.from({ length: Math.min(12, files.length) }, async () => {
      while (cursor < files.length) {
        const uri = files[cursor++];
        try {
          const text = new TextDecoder("utf-8").decode(await vscode.workspace.fs.readFile(uri));
          const lineStarts = lineStartsFor(text);
          const symbols = collectSymbols(text);
          const add = (s, kind, owner) => entries.push({
            name: s.name, detail: s.detail || "", kind, owner,
            uri: uri.toString(), offset: s.nameOffset,
            range: (() => { const start = positionAt(text, s.nameOffset, lineStarts); return { start, end: { line: start.line, character: start.character + s.name.length } }; })(),
          });
          for (const c of symbols.classes) {
            add(c, c.kind === "enum" ? 10 : 5, "");
            for (const m of c.methods) add({ ...m, detail: `${c.name}::${m.name}(${m.paramsRaw || ""}): ${m.ret}` }, 6, c.name);
            for (const f of c.fields) add({ ...f, detail: `${c.name}.$${f.name}: ${f.type}` }, 8, c.name);
            for (const k of c.constants) add({ ...k, detail: `${c.name}::${k.name}` }, 14, c.name);
          }
          for (const f of symbols.functions) add({ ...f, detail: `(${f.paramsRaw || ""}): ${f.ret}` }, 12, "");
          for (const c of symbols.constants) add({ ...c, detail: "constant" }, 14, "");
          entries.push({ uri: uri.toString(), text });
        } catch (_) { /* unreadable, generated, or concurrently removed file */ }
      }
    });
    await Promise.all(workers);
    return entries;
  })().catch((err) => { workspaceIndexPromise = null; throw err; });
  return workspaceIndexPromise;
}

function registerIndexInvalidation(ctx) {
  const watcher = vscode.workspace.createFileSystemWatcher("**/*.hphp");
  const invalidate = () => { workspaceIndexPromise = null; };
  ctx.subscriptions.push(watcher, watcher.onDidChange(invalidate), watcher.onDidCreate(invalidate), watcher.onDidDelete(invalidate));
}

function registerServerWorkspaceRequests(client) {
  if (typeof client.onRequest !== "function") return;
  client.onRequest("holyphp/findDefinition", async ({ name, owner }) => {
    const entries = await buildWorkspaceIndex();
    const open = new Set(vscode.workspace.textDocuments.map((d) => d.uri.toString()));
    const result = entries.filter((x) => x.name === name && x.range && !open.has(x.uri) && (!owner || x.owner === owner || x.kind === 5 || x.kind === 10));
    return result.slice(0, 20).map((x) => ({ uri: x.uri, range: x.range }));
  });
  client.onRequest("holyphp/findWorkspaceSymbols", async ({ query, excludeUris }) => {
    const entries = await buildWorkspaceIndex(), excluded = new Set(excludeUris || []), q = String(query || "").toLowerCase();
    return entries.filter((x) => x.range && !excluded.has(x.uri) && (!q || x.name.toLowerCase().includes(q)))
      .slice(0, 1000).map((x) => ({ name: x.name, kind: x.kind, containerName: x.owner || x.detail, location: { uri: x.uri, range: x.range } }));
  });
  client.onRequest("holyphp/findReferences", async ({ name, excludeUris }) => {
    const entries = await buildWorkspaceIndex(), excluded = new Set(excludeUris || []), re = new RegExp(`\\b${name}\\b`, "g"), locations = [];
    for (const file of entries) {
      if (!file.text || excluded.has(file.uri)) continue;
      const code = stripNonCode(file.text), starts = lineStartsFor(code); let match;
      while ((match = re.exec(code)) && locations.length < 2000) {
        const start = positionAt(code, match.index, starts);
        locations.push({ uri: file.uri, range: { start, end: { line: start.line, character: start.character + name.length } } });
      }
      if (locations.length >= 2000) break;
    }
    return locations;
  });
  client.onRequest("holyphp/rename", async ({ name, newName, excludeUris }) => {
    const entries = await buildWorkspaceIndex(), excluded = new Set(excludeUris || []), re = new RegExp(`\\b${name}\\b`, "g"), changes = {};
    for (const file of entries) {
      if (!file.text || excluded.has(file.uri)) continue;
      const code = stripNonCode(file.text), starts = lineStartsFor(code), edits = [];
      let match;
      while ((match = re.exec(code))) {
        const start = positionAt(code, match.index, starts);
        edits.push({ range: { start, end: { line: start.line, character: start.character + name.length } }, newText: newName });
      }
      if (edits.length) changes[file.uri] = edits;
    }
    return changes;
  });
  client.onRequest("holyphp/workspaceImports", async ({ uri, prefix }) => {
    const entries = await buildWorkspaceIndex(), source = vscode.Uri.parse(uri), names = new Set();
    for (const file of entries) {
      if (!file.text || file.uri === uri || !file.uri.startsWith("file:")) continue;
      const target = vscode.Uri.parse(file.uri).fsPath;
      let relative = path.relative(path.dirname(source.fsPath), target).replace(/\\/g, "/").replace(/\.hphp$/i, "");
      if (!relative.startsWith(".")) relative = "./" + relative;
      if (relative.startsWith(prefix || "")) names.add(relative);
    }
    return [...names].sort((a, b) => a.localeCompare(b)).slice(0, 100);
  });
}

/* ---------------- live diagnostics ---------------- */

const TEMP_PREFIX = ".hphp-check-";
let tempSeq = 0;

function tempPathFor(doc) {
  const dir = doc.uri.scheme === "file" ? path.dirname(doc.uri.fsPath) : os.tmpdir();
  return path.join(dir, `${TEMP_PREFIX}${process.pid}-${++tempSeq}.hphp`);
}

function linesOf(doc) {
  const out = [];
  for (let i = 0; i < doc.lineCount; i++) out.push(doc.lineAt(i).text);
  return out;
}

class Checker {
  constructor(collection, status, log) {
    this.collection = collection;
    this.status = status;
    this.log = log;
    this.running = null;        // {uri, child, temp}
    this.pending = new Map();   // uri -> {doc, version}
    this.timers = new Map();    // uri -> Timeout
    this.related = new Map();   // docUri -> Set<fileUri string>
    this.missingShown = false;
  }

  schedule(doc, immediate) {
    if (!doc || doc.languageId !== "holyphp") return;
    if (!cfg().get("diagnostics", true)) {
      this.collection.delete(doc.uri);
      return;
    }
    /* "save" mode: only explicit runs (save/open/command) check. */
    if (!immediate && cfg().get("diagnosticsMode", "type") === "save") return;
    const key = doc.uri.toString();
    const t = this.timers.get(key);
    if (t) clearTimeout(t);
    const delay = immediate ? 0 : Math.max(0, cfg().get("diagnosticsDebounce", 300));
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      this.enqueue(doc);
    }, delay));
  }

  enqueue(doc) {
    if (doc.isClosed) return;
    this.pending.set(doc.uri.toString(), { doc, version: doc.version });
    if (!this.running) this.drain();
  }

  drain() {
    for (const [key, job] of this.pending) {
      this.pending.delete(key);
      if (job.doc.isClosed) continue;
      this.run(job);
      return;
    }
  }

  run(job) {
    const doc = job.doc;
    const hphp = cfg().get("hphpPath", "hphp");
    let target = doc.uri.fsPath || "";
    let temp = null;

    if (doc.isDirty || doc.uri.scheme !== "file") {
      temp = tempPathFor(doc);
      try {
        fs.writeFileSync(temp, doc.getText(), "utf8");
      } catch (e) {
        this.log(`cannot write ${temp}: ${e.message}`);
        this.drain();
        return;
      }
      target = temp;
    }

    const cwd = path.dirname(target);
    this.setStatus("$(sync~spin) HolyPHP");
    const child = execFile(
      hphp,
      ["check", target],
      { cwd, windowsHide: true, timeout: 10000 },
      (err, stdout, stderr) => {
        this.running = null;
        if (temp) { try { fs.unlinkSync(temp); } catch (_) { /* gone */ } }
        if (err && err.code === "ENOENT") {
          this.reportMissing(hphp);
          this.drain();
          return;
        }
        this.apply(doc, job.version, String(stdout || "") + String(stderr || ""), target);
        this.drain();
      }
    );
    this.running = { uri: doc.uri.toString(), child, temp };
  }

  apply(doc, version, output, target) {
    /* The document was closed while the check ran: its diagnostics are gone. */
    if (doc.isClosed) return;
    /* A newer keystroke is already queued: its result would be overwritten
     * anyway, so skip the flicker. */
    if (doc.version !== version && doc.isDirty) return;

    const entries = parseCompilerOutput(output);
    const lines = linesOf(doc);
    const byFile = new Map();   // uriString -> Diagnostic[]

    for (const e of entries) {
      if (e.file && samePath(e.file, target)) {
        const r = rangeFor(lines, e.line, e.col, e.message);
        const d = new vscode.Diagnostic(
          new vscode.Range(r.line, r.start, r.line, r.end),
          e.message,
          e.severity === "warning" ? vscode.DiagnosticSeverity.Warning
            : e.severity === "note" ? vscode.DiagnosticSeverity.Information
              : vscode.DiagnosticSeverity.Error
        );
        d.source = "holyphp";
        const key = doc.uri.toString();
        if (!byFile.has(key)) byFile.set(key, []);
        byFile.get(key).push(d);
        continue;
      }
      if (!e.file) continue;

      /* an error in an imported file: resolve it relative to the check's cwd */
      const abs = path.isAbsolute(e.file) ? e.file : path.resolve(path.dirname(target), e.file);
      const uri = vscode.Uri.file(abs);
      const open = vscode.workspace.textDocuments.find(
        (d) => d.uri.scheme === "file" && samePath(d.uri.fsPath, abs));
      const fileLines = open ? linesOf(open) : (() => {
        try { return fs.readFileSync(abs, "utf8").split(/\r?\n/); } catch (_) { return []; }
      })();
      const r = rangeFor(fileLines, e.line, e.col, e.message);
      const d = new vscode.Diagnostic(
        new vscode.Range(r.line, r.start, r.line, r.end),
        e.message,
        e.severity === "warning" ? vscode.DiagnosticSeverity.Warning
          : vscode.DiagnosticSeverity.Error
      );
      d.source = "holyphp";
      const key = uri.toString();
      if (!byFile.has(key)) byFile.set(key, []);
      byFile.get(key).push(d);
    }

    /* clear the files this document's previous run touched */
    const previous = this.related.get(doc.uri.toString()) || new Set();
    for (const key of previous) {
      if (!byFile.has(key)) this.collection.delete(vscode.Uri.parse(key));
    }
    this.related.set(doc.uri.toString(), new Set(byFile.keys()));

    if (!byFile.has(doc.uri.toString())) {
      this.collection.set(doc.uri, []);
    }
    for (const [key, diags] of byFile) {
      this.collection.set(vscode.Uri.parse(key), diags);
    }
    this.updateStatus();
  }

  reportMissing(hphp) {
    this.collection.clear();
    this.setStatus("$(alert) hphp not found");
    if (this.missingShown) return;
    this.missingShown = true;
    vscode.window.showWarningMessage(
      `HolyPHP: could not run "${hphp}". Set "holyphp.hphpPath" to the compiler binary.`,
      "Open Settings"
    ).then((choice) => {
      if (choice === "Open Settings") {
        vscode.commands.executeCommand("workbench.action.openSettings", "holyphp.hphpPath");
      }
    });
  }

  setStatus(text) {
    this.status.text = text;
    this.status.show();
  }

  updateStatus() {
    const ed = vscode.window.activeTextEditor;
    if (!ed || ed.document.languageId !== "holyphp") {
      this.setStatus("HolyPHP");
      return;
    }
    const diags = this.collection.get(ed.document.uri) || [];
    const errors = diags.filter((d) => d.severity === vscode.DiagnosticSeverity.Error).length;
    const warnings = diags.length - errors;
    if (errors) {
      this.status.text = `$(error) ${errors}` + (warnings ? ` $(warning) ${warnings}` : "");
      this.status.tooltip = `${errors} error(s), ${warnings} warning(s) — click to re-check`;
    } else if (warnings) {
      this.status.text = `$(warning) ${warnings}`;
      this.status.tooltip = `${warnings} warning(s) — click to re-check`;
    } else {
      this.status.text = "$(check) HolyPHP";
      this.status.tooltip = "No problems — syntax, types, borrows and names check out";
    }
  }

  clear(doc) {
    const key = doc.uri.toString();
    const t = this.timers.get(key);
    if (t) { clearTimeout(t); this.timers.delete(key); }
    this.pending.delete(key);
    this.collection.delete(doc.uri);
    const prev = this.related.get(key);
    if (prev) for (const k of prev) this.collection.delete(vscode.Uri.parse(k));
    this.related.delete(key);
  }
}

function registerDiagnostics(ctx, log) {
  const collection = vscode.languages.createDiagnosticCollection("holyphp");
  ctx.subscriptions.push(collection);

  const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  status.command = "holyphp.check";
  status.text = "HolyPHP";
  status.tooltip = "Run the HolyPHP checker on this file";
  status.show();
  ctx.subscriptions.push(status);

  const checker = new Checker(collection, status, log);

  ctx.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((e) => checker.schedule(e.document, false)),
    vscode.workspace.onDidSaveTextDocument((doc) => checker.schedule(doc, true)),
    vscode.workspace.onDidOpenTextDocument((doc) => checker.schedule(doc, true)),
    vscode.workspace.onDidCloseTextDocument((doc) => checker.clear(doc)),
    vscode.window.onDidChangeActiveTextEditor(() => checker.updateStatus()),
    vscode.commands.registerCommand("holyphp.check", () => {
      const ed = vscode.window.activeTextEditor;
      if (ed) checker.schedule(ed.document, true);
    })
  );

  /* re-check when the compiler path or the mode changes */
  ctx.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (!e.affectsConfiguration("holyphp")) return;
      for (const doc of vscode.workspace.textDocuments) {
        if (doc.languageId === "holyphp") checker.schedule(doc, true);
      }
    })
  );

  /* first pass for everything that is already open */
  for (const doc of vscode.workspace.textDocuments) checker.schedule(doc, true);

  return checker;
}

/* ---------------- run command ---------------- */

function registerRun(ctx) {
  ctx.subscriptions.push(
    vscode.commands.registerCommand("holyphp.run", async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed || ed.document.languageId !== "holyphp") return;
      const hphp = cfg().get("hphpPath", "hphp");
      const file = ed.document.uri.fsPath;
      if (!file) {
        vscode.window.showWarningMessage("HolyPHP: save the file before running it.");
        return;
      }
      const term = vscode.window.terminals.find((t) => t.name === "HolyPHP") ||
        vscode.window.createTerminal("HolyPHP");
      term.show();
      term.sendText(`"${hphp}" run "${file}"`);
    })
  );
}

/* ---------------- activation ---------------- */

function activate(ctx) {
  const log = (msg) => console.log("holyphp: " + msg);
  registerBuiltinReference(ctx);
  registerIndexInvalidation(ctx);
  registerDiagnostics(ctx, log);
  registerRun(ctx);
  const serverModule = contextPath(ctx, "server.bundle.js");
  const serverOptions = { run: { module: serverModule, transport: TransportKind.stdio }, debug: { module: serverModule, transport: TransportKind.stdio, options: { execArgv: ["--nolazy", "--inspect=6009"] } } };
  const clientOptions = { documentSelector: [{ scheme: "file", language: "holyphp" }, { scheme: "untitled", language: "holyphp" }], synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher("**/*.hphp") } };
  const client = new LanguageClient("holyphp", "HolyPHP Language Server", serverOptions, clientOptions);
  registerServerWorkspaceRequests(client);
  ctx.subscriptions.push(client);
  client.start().then(() => log("language server ready"), (err) => log("language server failed: " + (err && err.stack || err)));
  log("language server started");
}

function contextPath(ctx, name) { return path.join(ctx.extensionPath, name); }

function deactivate() {}

module.exports = { activate, deactivate, _internals: { Checker, parseCompilerOutput, rangeFor } };
