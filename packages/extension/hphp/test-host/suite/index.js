"use strict";
const path = require("node:path");
const fs = require("node:fs");
const vscode = require("vscode");
const assert = require("node:assert/strict");

function waitFor(fn, timeoutMs = 12000) {
  const end = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const tick = async () => {
      try { const value = await fn(); if (value) return resolve(value); }
      catch (err) { return reject(err); }
      if (Date.now() >= end) return reject(new Error("Timed out waiting for VS Code language service"));
      setTimeout(tick, 100);
    };
    tick();
  });
}

async function extensionHostChecks() {
    const extension = vscode.extensions.getExtension("holyphp.holyphp");
    assert.ok(extension, "HolyPHP extension was not loaded by the host");
    await extension.activate();
    const root = vscode.workspace.workspaceFolders[0].uri.fsPath;
    const personPath = path.join(root, "Person.hphp");
    fs.writeFileSync(personPath, `<?php\nclass Person {\n  public string $name;\n  public function greet(): string { return "hi"; }\n}\n`, "utf8");
    const mainPath = path.join(root, "main.hphp");
    fs.writeFileSync(mainPath, `<?php\nfunction local(): int {\nreturn 1;\n}\nlet $p: Person = new Person();\n$p->greet();\nstrlen("x");\nstrln("x");\n`, "utf8");

    await vscode.workspace.getConfiguration("holyphp").update("hphpPath", process.env.HOLYPHP_TEST_COMPILER, vscode.ConfigurationTarget.Global);
    const uri = vscode.Uri.file(mainPath);
    const doc = await vscode.workspace.openTextDocument(uri);
    assert.equal(doc.languageId, "holyphp", `unexpected language id ${doc.languageId}`);
    const hover = await waitFor(async () => {
      const result = await vscode.commands.executeCommand("vscode.executeHoverProvider", uri, new vscode.Position(6, 2));
      return result && result.length ? result : null;
    });
    assert.ok(hover.length, "builtin hover missing in the editor host");

    const classLine = doc.lineAt(4).text;
    const classDefinition = await vscode.commands.executeCommand("vscode.executeDefinitionProvider", uri, new vscode.Position(4, classLine.indexOf("Person") + 2));
    assert.ok(classDefinition?.some((location) => location.uri.fsPath === personPath), "CTRL+click class navigation did not find the closed file");
    const methodLine = doc.lineAt(5).text;
    const methodDefinition = await vscode.commands.executeCommand("vscode.executeDefinitionProvider", uri, new vscode.Position(5, methodLine.indexOf("greet") + 1));
    assert.ok(methodDefinition?.some((location) => location.uri.fsPath === personPath), "CTRL+click method navigation did not find the closed file");
    const methodReferences = await vscode.commands.executeCommand("vscode.executeReferenceProvider", uri, new vscode.Position(5, methodLine.indexOf("greet") + 1));
    assert.ok(methodReferences?.some((location) => location.uri.fsPath === personPath), "reference search did not include the closed definition file");
    const rename = await vscode.commands.executeCommand("vscode.executeDocumentRenameProvider", uri, new vscode.Position(5, methodLine.indexOf("greet") + 1), "welcome");
    assert.ok(rename.entries().some(([target]) => target.fsPath === personPath), "rename did not edit the closed definition file");
    const symbols = await vscode.commands.executeCommand("vscode.executeWorkspaceSymbolProvider", "Person");
    assert.ok(symbols?.some((symbol) => symbol.name === "Person"), "closed-file workspace symbol missing");
    const importPath = path.join(root, "importer.hphp");
    fs.writeFileSync(importPath, `<?php\nimport "./";\n`, "utf8");
    const importUri = vscode.Uri.file(importPath);
    await vscode.workspace.openTextDocument(importUri);
    const importItems = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", importUri, new vscode.Position(1, 10));
    const importItemsList = Array.isArray(importItems) ? importItems : importItems && importItems.items;
    assert.ok(importItemsList?.some((item) => item.label === "./Person"), "workspace import completion missing in the editor host");

    const diagnostics = await waitFor(() => {
      const items = vscode.languages.getDiagnostics(uri);
      return items.some((item) => /undefined function/.test(item.message)) ? items : null;
    });
    const diagnostic = diagnostics.find((item) => /undefined function/.test(item.message));
    const actions = await vscode.commands.executeCommand("vscode.executeCodeActionProvider", uri, diagnostic.range, "quickfix");
    assert.ok(actions?.some((action) => /Replace with 'strlen'/.test(action.title)), "builtin typo quick fix missing in the editor host");

    const edits = await vscode.commands.executeCommand("vscode.executeFormatDocumentProvider", uri, { tabSize: 2, insertSpaces: true });
    assert.ok(Array.isArray(edits), "format provider was not available");
    assert.ok(edits.some((edit) => edit.range.start.line === 2 && edit.newText === "  "), "formatter did not indent the function body");
    assert.equal(doc.languageId, "holyphp");
}

module.exports.run = async () => {
  const Mocha = require("mocha");
  const mocha = new Mocha({ ui: "tdd", timeout: 30000 });
  const suite = Mocha.Suite.create(mocha.suite, "HolyPHP VS Code extension host");
  suite.addTest(new Mocha.Test("LSP navigation, closed-file index, diagnostics, code actions and formatting", extensionHostChecks));
  await new Promise((resolve, reject) => mocha.run((failures) => failures ? reject(new Error(`${failures} extension host test(s) failed`)) : resolve()));
};
