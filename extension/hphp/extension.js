/* extension.js — HolyPHP language support for VS Code.
 *
 * Features:
 *   - Syntax highlighting via TextMate grammar (PHP-flavored)
 *   - Autocomplete: keywords, 180+ builtins (generated from the compiler
 *     table), $variables in scope, class members
 *   - Hover: builtin signatures + descriptions
 *   - Live diagnostics: runs `hphp check` and maps compiler errors to
 *     squiggles (on save or on type, configurable)
 *   - Commands: "HolyPHP: Check current file", "HolyPHP: Run current file"
 */
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const { execFile } = require("child_process");

/* ---------------- static data ---------------- */

const KEYWORDS = [
  ["if", "kw", "if (cond) { ... }"],
  ["else", "kw", "else { ... }"],
  ["elseif", "kw", "elseif (cond) { ... }"],
  ["while", "kw", "while (cond) { ... }"],
  ["for", "kw", "for (init; cond; step) { ... }"],
  ["foreach", "kw", "foreach (arr as $k => $v) { ... }"],
  ["as", "kw", "foreach (arr as $v)"],
  ["switch", "kw", "switch (x) { case v: ... break; }"],
  ["case", "kw", "case value:"],
  ["default", "kw", "default:"],
  ["do", "kw", "do { ... } while (cond);"],
  ["match", "kw", "match (x) { pat => val, default => val }"],
  ["return", "kw", "return expr;"],
  ["break", "kw", "break; / break 2;"],
  ["continue", "kw", "continue;"],
  ["try", "kw", "try { ... } catch ($e) { ... }"],
  ["catch", "kw", "catch (Exception $e) { ... }"],
  ["finally", "kw", "finally { ... }"],
  ["throw", "kw", "throw new Exception(\"msg\");"],
  ["function", "kw", "function name(int $p): int { ... }"],
  ["class", "kw", "class Name { public int $f; }"],
  ["interface", "kw", "interface Name { ... }"],
  ["trait", "kw", "trait Name { ... }"],
  ["enum", "kw", "enum Color { Red, Green }"],
  ["extends", "kw", "class A extends B"],
  ["implements", "kw", "class A implements I"],
  ["new", "kw", "new Class(args)"],
  ["this", "kw", "$this->field"],
  ["self", "kw", "self::method()"],
  ["parent", "kw", "parent::__construct()"],
  ["public", "kw", "visibility modifier"],
  ["protected", "kw", "visibility modifier"],
  ["private", "kw", "visibility modifier"],
  ["static", "kw", "static member"],
  ["abstract", "kw", "abstract class/method"],
  ["final", "kw", "final class/method"],
  ["const", "kw", "const NAME = value;"],
  ["echo", "stmt", 'echo "text";'],
  ["print", "stmt", "print expr;"],
  ["unsafe", "kw", "unsafe { *ptr ... } — raw pointer block"],
  ["own", "kw", "own T — exclusive ownership"],
  ["mut", "kw", "mut — mutable binding/reference"],
  ["let", "kw", "let $x = ...; typed declaration"],
  ["use", "kw", "use Name;"],
  ["import", "kw", "import module;"],
];

const TYPES = [
  "int", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "isize",
  "float", "f32", "f64", "double", "bool", "string", "str", "array", "list",
  "mixed", "void", "Vec", "Map", "Set", "Option", "Result",
];

const SNIPPET_COMPLETIONS = [
  new vscode.CompletionItem("fore", vscode.CompletionItemKind.Snippet),
  new vscode.CompletionItem("foreach with key", vscode.CompletionItemKind.Snippet),
].map((c) => (c.insertText = undefined, c));

let BUILTINS = [];
try {
  const data = JSON.parse(
    fs.readFileSync(path.join(__dirname, "data", "builtins.json"), "utf8")
  );
  BUILTINS = data.builtins || [];
} catch (e) {
  console.error("holyphp: builtins.json missing — run scripts/genext.js");
}

const builtinByName = new Map(BUILTINS.map((b) => [b.name, b]));

/* ---------------- document symbol extraction ---------------- */

/* collect $variables declared in the document (best-effort, line based) */
function collectVariables(text) {
  const vars = new Set();
  const re = /\$([A-Za-z_][A-Za-z0-9_]*)/g;
  let m;
  while ((m = re.exec(text)) !== null) vars.add(m[1]);
  // superglobals always available
  ["argc", "argv", "_SERVER", "_GET", "_POST", "_ENV"].forEach((v) => vars.add(v));
  return [...vars];
}

/* collect user-defined function names: function name( */
function collectFunctions(text) {
  const fns = [];
  const re = /\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)(?:\s*:\s*([A-Za-z_][A-Za-z0-9_<>]*))?/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    fns.push({ name: m[1], params: (m[2] || "").trim(), ret: m[3] || "mixed" });
  }
  return fns;
}

/* collect class names */
function collectClasses(text) {
  const out = [];
  const re = /\b(?:class|interface|trait)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
  let m;
  while ((m = re.exec(text)) !== null) out.push(m[1]);
  return out;
}

/* ---------------- completion provider ---------------- */

function makeKeywordItem(word, kind, detail) {
  const item = new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword);
  item.detail = "HolyPHP " + (kind === "stmt" ? "statement" : "keyword");
  item.documentation = new vscode.MarkdownString().appendCodeblock(detail, "holyphp");
  return item;
}

function makeTypeItem(t) {
  const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.TypeParameter);
  item.detail = "HolyPHP type";
  return item;
}

const documentSelector = [{ language: "holyphp", scheme: "file" }, { language: "holyphp", scheme: "untitled" }];

function registerCompletion(ctx) {
  const provider = vscode.languages.registerCompletionItemProvider(
    documentSelector,
    {
      provideCompletionItems(doc, pos) {
        const linePrefix = doc.lineAt(pos).text.slice(0, pos.character);
        const text = doc.getText();
        const items = [];

        /* after "->" or "::": class members from the document (best effort) */
        if (/->\s*[\w]*$/.test(linePrefix) || /::\s*[\w]*$/.test(linePrefix)) {
          const re = /->\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(/g;
          let m;
          const seen = new Set();
          while ((m = re.exec(text)) !== null) {
            if (!seen.has(m[1])) {
              seen.add(m[1]);
              const it = new vscode.CompletionItem(m[1], vscode.CompletionItemKind.Method);
              it.detail = "method";
              items.push(it);
            }
          }
          const fre = /->\s*([A-Za-z_][A-Za-z0-9_]*)\s*[^(\s]/g;
          while ((m = fre.exec(text)) !== null) {
            if (!seen.has(m[1])) {
              seen.add(m[1]);
              const it = new vscode.CompletionItem(m[1], vscode.CompletionItemKind.Field);
              it.detail = "field";
              items.push(it);
            }
          }
          return items;
        }

        /* variables */
        for (const v of collectVariables(text)) {
          const item = new vscode.CompletionItem("$" + v, vscode.CompletionItemKind.Variable);
          item.sortText = "0" + v;
          items.push(item);
        }

        /* user functions with full signature */
        for (const f of collectFunctions(text)) {
          const item = new vscode.CompletionItem(
            f.name,
            vscode.CompletionItemKind.Function
          );
          const params = f.params
            ? f.params.split(",").map((p) => p.trim().split(/\s+/).pop().replace("$", "$"))
            : [];
          item.insertText = new vscode.SnippetString(
            f.name + "(" + params.map((p, i) => "${" + (i + 1) + ":" + p + "}").join(", ") + ")$0"
          );
          item.detail = `function (${f.params}): ${f.ret}`;
          item.documentation = new vscode.MarkdownString().appendCodeblock(
            `function ${f.name}(${f.params}): ${f.ret}`,
            "holyphp"
          );
          item.sortText = "1" + f.name;
          items.push(item);
        }

        /* builtins (generated from the compiler table) */
        for (const b of BUILTINS) {
          const item = new vscode.CompletionItem(
            b.name,
            vscode.CompletionItemKind.Function
          );
          item.detail = b.sig + (b.unsafe ? "  (unsafe)" : "");
          item.documentation = new vscode.MarkdownString(
            (b.desc || "builtin function") +
              (b.unsafe ? "\n\n⚠️ only callable inside `unsafe` blocks" : "")
          ).appendCodeblock(b.sig, "holyphp");
          if (b.args === 1) item.insertText = new vscode.SnippetString(b.name + "(${1:arg})$0");
          else if (b.args === 0) item.insertText = b.name + "()";
          item.sortText = "2" + b.name;
          items.push(item);
        }

        /* classes */
        for (const c of collectClasses(text)) {
          items.push(
            new vscode.CompletionItem(c, vscode.CompletionItemKind.Class)
          );
        }

        /* keywords */
        for (const [w, kind, detail] of KEYWORDS) items.push(makeKeywordItem(w, kind, detail));

        /* types */
        for (const t of TYPES) items.push(makeTypeItem(t));

        return items;
      },
    },
    "$", "->", "::"
  );
  ctx.subscriptions.push(provider);
}

/* ---------------- hover provider ---------------- */

function registerHover(ctx) {
  const provider = vscode.languages.registerHoverProvider(documentSelector, {
    provideHover(doc, pos) {
      const range = doc.getWordRangeAtPosition(pos, /[$A-Za-z_][A-Za-z0-9_]*/);
      if (!range) return;
      const word = doc.getText(range);
      const bare = word.replace(/^\$/, "");

      const md = new vscode.MarkdownString();

      const b = builtinByName.get(bare);
      if (b) {
        md.appendMarkdown("**" + b.name + "** — builtin\n\n");
        md.appendCodeblock(b.sig, "holyphp");
        md.appendText(b.desc || "");
        if (b.unsafe) md.appendMarkdown("\n\n⚠️ **unsafe**: only callable inside `unsafe` blocks");
        return new vscode.Hover(md, range);
      }

      const fn = collectFunctions(doc.getText()).find((f) => f.name === bare);
      if (fn) {
        md.appendMarkdown("**" + fn.name + "** — function\n\n");
        md.appendCodeblock(`function ${fn.name}(${fn.params}): ${fn.ret}`, "holyphp");
        return new vscode.Hover(md, range);
      }

      if (word.startsWith("$")) {
        md.appendMarkdown("**" + word + "** — variable");
        return new vscode.Hover(md, range);
      }
      return null;
    },
  });
  ctx.subscriptions.push(provider);
}

/* ---------------- diagnostics (hphp check) ---------------- */

/* Parse lines like:  file.hphp:12:5: error: message   (col is optional) */
const DIAG_RE = /^(.+?):(\d+):(\d+):\s*(error|warning):\s*(.+)$/;
const DIAG_NO_COL_RE = /^(.+?):(\d+):\s*(error|warning):\s*(.+)$/;

function parseDiagnostics(out, doc) {
  const diags = [];
  for (const raw of out.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) continue;
    let m = DIAG_RE.exec(line) || DIAG_NO_COL_RE.exec(line);
    if (!m) continue;
    const [, file, lnS, colOrSev, sevOrMsg, msgOrNothing] = m;
    let ln = parseInt(lnS, 10) - 1;
    let col = 0;
    let sev, msg;
    if (msgOrNothing === undefined) {
      // no-column form: colOrSev holds severity
      sev = colOrSev;
      msg = sevOrMsg;
    } else {
      col = Math.max(0, parseInt(colOrSev, 10) - 1);
      sev = sevOrMsg;
      msg = msgOrNothing;
    }
    if (ln < 0) ln = 0;
    const docLine = doc.lineCount > ln ? doc.lineAt(ln) : null;
    let start = col;
    let end;
    if (docLine) {
      end = docLine.range.end.character;
      if (end <= start) start = 0;
    } else {
      start = 0;
      end = 0;
      ln = Math.max(0, doc.lineCount - 1);
    }
    const range = new vscode.Range(
      ln,
      start,
      ln,
      Math.max(end, start + 1)
    );
    diags.push(
      new vscode.Diagnostic(
        range,
        msg,
        sev === "warning" ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
      )
    );
  }
  return diags;
}

class HphpCheckRunner {
  constructor() {
    this.buf = "";
    this.running = false;
  }

  /* serialize: only one hphp check at a time; last request wins */
  run(doc, diags) {
    this.pending = { doc, diags };
    if (this.running) return;
    this.drain();
  }

  drain() {
    const job = this.pending;
    this.pending = null;
    if (!job || job.doc.isClosed()) {
      if (this.pending) this.drain();
      return;
    }
    this.running = true;
    const cfg = vscode.workspace.getConfiguration("holyphp");
    const hphp = cfg.get("hphpPath", "hphp");
    const file = job.doc.uri.fsPath;
    execFile(
      hphp,
      ["check", file],
      { cwd: path.dirname(file), windowsHide: true, timeout: 20000 },
      (err, stdout, stderr) => {
        this.running = false;
        const out = String(stdout || "") + String(stderr || "");
        const found = parseDiagnostics(out, job.doc);
        job.diags.set(job.doc.uri, found);
        if (this.pending) this.drain();
      }
    );
  }
}

function registerDiagnostics(ctx) {
  const collection =
    vscode.languages.createDiagnosticCollection("holyphp");
  ctx.subscriptions.push(collection);
  const runner = new HphpCheckRunner();

  const checkDoc = (doc) => {
    if (doc.languageId !== "holyphp") return;
    const cfg = vscode.workspace.getConfiguration("holyphp");
    if (!cfg.get("diagnostics", true)) return;
    runner.run(doc, collection);
  };

  const mode = () =>
    vscode.workspace.getConfiguration("holyphp").get("diagnosticsMode", "save");

  ctx.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument(checkDoc),
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (mode() !== "type") return;
      checkDoc(e.document);
    }),
    vscode.workspace.onDidOpenTextDocument(checkDoc),
    vscode.commands.registerCommand("holyphp.check", () => {
      const ed = vscode.window.activeTextEditor;
      if (ed) checkDoc(ed.document);
    })
  );

  /* clear diagnostics for closed files */
  ctx.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument((d) => collection.delete(d.uri))
  );
}

/* ---------------- run command ---------------- */

function registerRun(ctx) {
  ctx.subscriptions.push(
    vscode.commands.registerCommand("holyphp.run", async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed || ed.document.languageId !== "holyphp") return;
      const cfg = vscode.workspace.getConfiguration("holyphp");
      const hphp = cfg.get("hphpPath", "hphp");
      const file = ed.document.uri.fsPath;
      const term = vscode.window.terminals.find(
        (t) => t.name === "HolyPHP"
      ) || vscode.window.createTerminal("HolyPHP");
      term.show();
      term.sendText(`hphp run "${file}"`);
    })
  );
}

/* ---------------- activation ---------------- */

function activate(ctx) {
  registerCompletion(ctx);
  registerHover(ctx);
  registerDiagnostics(ctx);
  registerRun(ctx);
  console.log("holyphp extension active");
}

function deactivate() {}

module.exports = { activate, deactivate };
