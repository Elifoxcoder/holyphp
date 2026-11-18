"use strict";
const path = require("node:path");
const fs = require("node:fs");
const { runTests } = require("@vscode/test-electron");

const extensionRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(extensionRoot, "../../..");
const vscodeExecutablePath = process.env.VSCODE_EXECUTABLE || path.join(process.env.LOCALAPPDATA || "", "Programs", "Microsoft VS Code", "Code.exe");
const testWorkspace = path.join(extensionRoot, ".vscode-test", "workspace");
fs.mkdirSync(testWorkspace, { recursive: true });

runTests({
  vscodeExecutablePath,
  extensionDevelopmentPath: extensionRoot,
  extensionTestsPath: path.join(__dirname, "suite", "index.js"),
  launchArgs: [testWorkspace, "--disable-gpu", "--disable-workspace-trust", "--disable-extensions"],
  extensionTestsEnv: { HOLYPHP_TEST_COMPILER: path.join(repoRoot, "hphp.exe") },
}).then((code) => process.exit(code), (err) => { console.error(err); process.exit(1); });
