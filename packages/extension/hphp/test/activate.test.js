/* Wiring smoke test; the actual LSP protocol is covered in lsp.integration.test.js. */
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");
const mock = require("./mock-vscode");

mock.install();
const ext = require(path.join(__dirname, "..", "extension.js"));
const { commands, clients, getStatusBar } = mock;

test("activation starts the LSP client and registers editor tools", () => {
  const ctx = { subscriptions: [], extensionPath: path.join(__dirname, "..") };
  ext.activate(ctx);
  assert.equal(clients.length, 1);
  assert.equal(clients[0].started, true);
  assert.deepEqual([...clients[0].requests.keys()].sort(), [
    "holyphp/findDefinition", "holyphp/findReferences", "holyphp/findWorkspaceSymbols", "holyphp/rename", "holyphp/workspaceImports",
  ]);
  assert.deepEqual(commands.map((c) => c.id).sort(), ["holyphp.builtins", "holyphp.check", "holyphp.run"]);
  assert.ok(getStatusBar(), "no compiler status item");
  assert.ok(ctx.subscriptions.length >= 5);
});
