import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const input = path.join(root, "packages/extension/hphp/data/builtins.json");
const output = path.join(root, "docs/assets/playground-completions.js");
const { builtins } = JSON.parse(fs.readFileSync(input, "utf8"));
const browserData = builtins.map(({ name, sig, desc, unsafe }) => ({
  name, signature: sig, description: desc, unsafe: Boolean(unsafe),
}));

fs.writeFileSync(
  output,
  "// Generated from packages/extension/hphp/data/builtins.json by scripts/gen-playground-completions.mjs\n" +
    "window.HPHP_BUILTINS = " + JSON.stringify(browserData) + ";\n",
);
console.log(`wrote ${browserData.length} playground completions`);
