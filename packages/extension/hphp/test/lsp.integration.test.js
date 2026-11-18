"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const { spawn } = require("node:child_process");
const path = require("node:path");

function startClient() {
  const server = spawn(process.execPath, [path.join(__dirname, "..", "server.js"), "--stdio"], { stdio: ["pipe", "pipe", "pipe"] });
  let buffer = Buffer.alloc(0), sequence = 1;
  const pending = new Map();
  server.stdout.on("data", (part) => {
    buffer = Buffer.concat([buffer, part]);
    while (true) {
      const boundary = buffer.indexOf("\r\n\r\n");
      if (boundary < 0) break;
      const header = buffer.subarray(0, boundary).toString("ascii");
      const length = +(header.match(/Content-Length:\s*(\d+)/i) || [])[1];
      if (!length || buffer.length < boundary + 4 + length) break;
      const message = JSON.parse(buffer.subarray(boundary + 4, boundary + 4 + length).toString("utf8"));
      buffer = buffer.subarray(boundary + 4 + length);
      if (message.method && message.id !== undefined) {
        const result = message.method === "holyphp/workspaceImports" ? ["./lib/module"] : [];
        send({ jsonrpc: "2.0", id: message.id, result });
      } else if (message.id !== undefined && pending.has(message.id)) {
        const entry = pending.get(message.id); pending.delete(message.id); clearTimeout(entry.timer);
        if (message.error) entry.reject(new Error(message.error.message)); else entry.resolve(message.result);
      }
    }
  });
  server.stderr.on("data", () => {});
  function send(message) {
    const body = Buffer.from(JSON.stringify(message), "utf8");
    server.stdin.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
  }
  function request(method, params) {
    const id = sequence++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { pending.delete(id); reject(new Error(`LSP request timed out: ${method}`)); }, 8000);
      pending.set(id, { resolve, reject, timer });
      send({ jsonrpc: "2.0", id, method, params });
    });
  }
  return { server, request, notify: (method, params) => send({ jsonrpc: "2.0", method, params }) };
}

test("LSP provides navigation, completion, quick fixes and formatting", async (t) => {
  const lsp = startClient();
  t.after(() => { if (!lsp.server.killed) lsp.server.kill(); });
  const uri = "file:///workspace/main.hphp";
  const text = `<?php
class Person {
public string $name;
public function greet(string $who): string {
return $who;
}
}
let $p: Person = new Person();
$p->greet("x");
strlen("x");
strl("x");
`;
  const initialized = await lsp.request("initialize", { processId: process.pid, rootUri: "file:///workspace", capabilities: {}, workspaceFolders: [] });
  assert.equal(initialized.serverInfo.name, "HolyPHP Language Server");
  assert.equal(initialized.capabilities.definitionProvider, true);
  assert.equal(initialized.capabilities.referencesProvider, true);
  assert.ok(initialized.capabilities.renameProvider);
  lsp.notify("initialized", {});
  lsp.notify("textDocument/didOpen", { textDocument: { uri, languageId: "holyphp", version: 1, text } });

  const completions = await lsp.request("textDocument/completion", { textDocument: { uri }, position: { line: 10, character: 4 } });
  assert.ok(completions.some((item) => item.label === "strlen"), "builtin completion missing");
  const hover = await lsp.request("textDocument/hover", { textDocument: { uri }, position: { line: 9, character: 2 } });
  assert.match(hover.contents.value, /strlen/);
  const definition = await lsp.request("textDocument/definition", { textDocument: { uri }, position: { line: 8, character: 6 } });
  assert.equal(definition.range.start.line, 3, "method definition target is wrong");
  const references = await lsp.request("textDocument/references", { textDocument: { uri }, position: { line: 8, character: 6 }, context: { includeDeclaration: false } });
  assert.ok(references.some((item) => item.range.start.line === 8));
  const rename = await lsp.request("textDocument/rename", { textDocument: { uri }, position: { line: 8, character: 6 }, newName: "welcome" });
  assert.ok(rename.changes[uri].some((item) => item.newText === "welcome"));
  const importUri = "file:///workspace/importer.hphp";
  lsp.notify("textDocument/didOpen", { textDocument: { uri: importUri, languageId: "holyphp", version: 1, text: '<?php\nimport "./";' } });
  const imports = await lsp.request("textDocument/completion", { textDocument: { uri: importUri }, position: { line: 1, character: 10 } });
  assert.ok(imports.some((item) => item.label === "./lib/module"), "workspace import completion missing");
  const fixes = await lsp.request("textDocument/codeAction", { textDocument: { uri }, range: { start: { line: 10, character: 0 }, end: { line: 10, character: 4 } }, context: { diagnostics: [{ message: "call to undefined function 'strl()'", range: { start: { line: 10, character: 0 }, end: { line: 10, character: 4 } }, severity: 1 }] } });
  assert.ok(fixes.some((action) => /strlen/.test(action.title)), "builtin typo quick fix missing");
  const formatting = await lsp.request("textDocument/formatting", { textDocument: { uri }, options: { tabSize: 2, insertSpaces: true } });
  assert.ok(formatting.length);
  assert.match(formatting[0].newText, /\n  public string \$name;/);
  await lsp.request("shutdown", null);
  lsp.notify("exit");
});
