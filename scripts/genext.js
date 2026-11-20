#!/usr/bin/env node
/*
 * genext.js — generates the extension's static data from the compiler:
 *
 *   1. packages/extension/hphp/data/builtins.json   (completions/hover)
 *   2. syntaxes/hphp.tmLanguage.json                (builtin + constant regexes)
 *
 * Why generate: completions, hover and highlighting then always match the
 * compiler exactly. Run after adding builtins:  node scripts/genext.js
 */
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const extDir = path.join(root, "packages", "extension", "hphp");
const builtinsC = fs.readFileSync(path.join(root, "src/hphpc/builtins.c"), "utf8");

/* hand-written docs for every builtin (see scripts/builtin-docs.js) */
const DOCS = require(path.join(__dirname, "builtin-docs.js"));
const EXAMPLES = require(path.join(__dirname, "builtin-examples.js"));
let docsMissing = [];

/* arg counts per BuiltinShape, mirroring builtins.h semantics */
const SHAPES = {
  B_STR1:    { n: 1, params: "string $s",                 desc: "string → string" },
  B_ZERO:    { n: 0, params: "",                          desc: "no arguments" },
  B_STR2:    { n: 2, params: "string $haystack, string $needle", desc: "(string, string) → string" },
  B_INT_STR: { n: 2, params: "int $times, string $s",     desc: "(int, string) → string" },
  B_STR_INT: { n: 1, params: "string $s",                 desc: "string → int" },
  B_LEN:     { n: 1, params: "mixed $v",                  desc: "string|array → int" },
  B_CMP:     { n: 2, params: "string $a, string $b",      desc: "(string, string) → int" },
  B_PRINT:   { n: -1, params: "...$values",               desc: "variadic print" },
  B_ARRAY:   { n: -1, params: "...$values",               desc: "variadic → array" },
  B_MAXMIN:  { n: -1, params: "...$numbers",              desc: "variadic numeric" },
  B_ABS:     { n: 1, params: "mixed $number",             desc: "number → number" },
  B_INT_INT: { n: 1, params: "int $n",                    desc: "int → int" },
  B_JSON:    { n: 1, params: "mixed $value",              desc: "mixed → string" },
  B_SPLIT:   { n: 2, params: "string $separator, string $s", desc: "(string, string) → array" },
  B_JOIN:    { n: 2, params: "mixed $glue, array $pieces", desc: "(glue, array) → string" },
  B_KEYS:    { n: 1, params: "array $a",                  desc: "array → array (keys)" },
  B_VALUES:  { n: 1, params: "array $a",                  desc: "array → array (values)" },
  B_SORTISH: { n: 1, params: "array $a",                  desc: "array → array" },
  B_RANGE:   { n: 2, params: "int $start, int $end",      desc: "(int, int) → array" },
  B_SORT:    { n: 1, params: "array &$a",                 desc: "in-place sort" },
  B_MISC:    { n: -2, params: "...$args",                 desc: "flexible signature" },
  /* colour-aware console I/O (optional trailing colour constant) */
  B_PRINTF_COLOR:  { n: 2, params: "mixed $value[, int $colour]", desc: "prints without a newline" },
  B_PRINTLN_COLOR: { n: 2, params: "mixed $value[, int $colour]", desc: "prints with a newline" },
  B_READLINE_COLOR:{ n: 2, params: "string $prompt[, int $colour]", desc: "prompt, then one line from stdin" },
};

/* pull the `add("name", SHAPE, ty_x, unsafe, variadic);` lines.
 * NOTE: shape names contain underscores (B_INT_INT, B_STR_INT, …) —
 * the character class must include "_". */
const re = /add\(\s*"([^"]+)"\s*,\s*(B_[A-Z0-9_]+)\s*,\s*(ty_[a-z0-9_]+|type_[a-z_()]+)\s*,\s*(true|false)\s*,\s*(true|false)\s*\)/g;
const types = { ty_int: "int", ty_string: "string", ty_bool: "bool", ty_float: "float", ty_mixed: "mixed", ty_void: "void" };

/* Pull the authored reference page into the extension bundle. These pages are
 * the source of truth for parameter notes, return semantics, examples, and
 * cross-links; keep parsing deliberately small and dependency-free. */
function decodeHtml(s) {
  return String(s || "").replace(/&quot;/g, '"').replace(/&#39;|&apos;/g, "'")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&")
    .replace(/<[^>]*>/g, "").replace(/\s+/g, " ").trim();
}
function referenceFor(name) {
  const authored = EXAMPLES[name];
  const file = path.join(root, "docs", "builtin", `${name}.html`);
  let fallback = {};
  if (fs.existsSync(file)) {
    const html = fs.readFileSync(file, "utf8");
    const section = (title) => {
      const re = new RegExp(`<h2>${title}<\\/h2>([\\s\\S]*?)(?=<h2>|<div class="nav"|<\\/main>)`, "i");
      const m = re.exec(html);
      return m ? m[1] : "";
    };
    const paramsHtml = section("Parameters");
    const params = [];
    for (const row of paramsHtml.matchAll(/<tr>([\s\S]*?)<\/tr>/gi)) {
      const cells = [...row[1].matchAll(/<td[^>]*>([\s\S]*?)<\/td>/gi)].map(x => decodeHtml(x[1]));
      if (cells.length >= 3 && cells[0].startsWith("$")) params.push({ name: cells[0], type: cells[1], description: cells[2] });
    }
    const returnHtml = section("Return value");
    const exampleHtml = section("Example");
    const example = /<pre>([\s\S]*?)<\/pre>/i.exec(exampleHtml);
    const output = /<pre class="out">([\s\S]*?)<\/pre>/i.exec(exampleHtml);
    const seeHtml = section("See also");
    fallback = {
      params,
      returns: decodeHtml((/<p class="ret">([\s\S]*?)<\/p>/i.exec(returnHtml) || [," "])[1]),
      example: example ? decodeHtml(example[1]) : "",
      exampleOutput: output ? decodeHtml(output[1]) : "",
      seeAlso: [...seeHtml.matchAll(/href="([^"]+)"/g)].map(x => path.basename(x[1], ".html")),
    };
  }
  if (!authored) return fallback;
  const params = (authored.params || []).map((p) => ({
    name: "$" + (p.n === "..." ? "args" : p.n), type: p.t || "mixed",
    description: p.d || "", optional: p.req === false, variadic: p.n === "...",
  }));
  const required = params.filter((p) => !p.optional).map((p) => `${p.type} ${p.variadic ? "..." : ""}${p.name}`);
  const optional = params.filter((p) => p.optional).map((p) => `${p.type} ${p.variadic ? "..." : ""}${p.name}`);
  const argText = required.join(", ") + (optional.length ? `[${required.length ? ", " : ""}${optional.join(", ")}]` : "");
  return {
    params: params.length ? params : fallback.params || [],
    returns: authored.ret || fallback.returns || "",
    example: authored.ex || fallback.example || "",
    exampleOutput: authored.out || fallback.exampleOutput || "",
    seeAlso: authored.see && authored.see.length ? authored.see : fallback.seeAlso || [],
    authoredNoCode: !!authored.nocode,
    authoredSignature: argText,
  };
}

let m, count = 0, entries = [];
while ((m = re.exec(builtinsC)) !== null) {
  const [, name, shape, ret, unsafe, variadic] = m;
  const sh = SHAPES[shape] || SHAPES.B_MISC;
  const doc = DOCS[name];
  if (!doc) docsMissing.push(name);
  const reference = referenceFor(name);
  entries.push({
    name,
    sig: reference.authoredSignature !== undefined
      ? `${name}(${reference.authoredSignature}): ${types[ret] || "mixed"}`
      : `${name}(${sh.params}): ${types[ret] || "mixed"}`,
    args: variadic === "true" ? -1 : sh.n,
    desc: doc || sh.desc,
    unsafe: unsafe === "true",
    ...reference,
  });
  count++;
}

if (docsMissing.length) {
  console.error(`genext: ${docsMissing.length} builtins without a description in scripts/builtin-docs.js:`);
  console.error("  " + docsMissing.join(", "));
  process.exit(1);
}

const outDir = path.join(extDir, "data");
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(
  path.join(outDir, "builtins.json"),
  JSON.stringify({ generatedFrom: "src/hphpc/builtins.c", builtins: entries }, null, 1)
);
console.log(`genext: wrote ${count} builtins to packages/extension/hphp/data/builtins.json`);
if (count < 50) { console.error("genext: suspiciously few builtins — table regex mismatch?"); process.exit(1); }

/* ---------------- syntax highlighting ----------------
 * The grammar's builtin list was hand-written once and drifted (60 names while
 * the compiler had hundreds). Generate both alternations from the same data. */

const { CONSTANTS } = require(path.join(extDir, "lib", "data.js"));

function alternation(names) {
  /* longest first: `str_replace` must win over a hypothetical `str` */
  const sorted = [...new Set(names)].sort((a, b) => b.length - a.length || a.localeCompare(b));
  return "\\b(?:" + sorted.join("|") + ")\\b";
}

const functionNames = entries.filter((e) => !CONSTANTS[e.name]).map((e) => e.name);
const constantNames = Object.keys(CONSTANTS);

const grammarPath = path.join(extDir, "syntaxes", "hphp.tmLanguage.json");
const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf8"));

/* Variables must be tried before constants/builtins: `$GREEN` is a variable,
 * not the colour constant followed by a stray `$`. */
grammar.patterns = [
  { include: "#comments" },
  { include: "#strings" },
  { include: "#variables" },
  { include: "#classes" },
  { include: "#members" },
  { include: "#keywords" },
  { include: "#constants" },
  { include: "#types" },
  { include: "#functions" },
  { include: "#numbers" },
  { include: "#operators" },
  { include: "#punctuation" },
];

grammar.repository.functions = {
  patterns: [
    {
      name: "entity.name.function.hphp",
      match: "\\b(?<=function\\s)[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()",
    },
    {
      name: "support.function.builtin.hphp",
      match: alternation(functionNames) + "\\s*(?=\\()",
    },
    {
      /* calls of the program's own functions (not $callback() or ->method()):
       * after the builtins, so a builtin keeps its own scope */
      name: "entity.name.function.php",
      match: "(?<![$>])\\b[A-Za-z_][A-Za-z0-9_]*\\b\\s*(?=\\()",
    },
  ],
};

grammar.repository.constants = {
  patterns: [
    {
      name: "constant.language.hphp",
      match: "\\b(true|TRUE|false|FALSE|null|NULL)\\b",
    },
    {
      name: "constant.other.predefined.hphp",
      match: alternation(constantNames),
    },
  ],
};

fs.writeFileSync(grammarPath, JSON.stringify(grammar, null, 2) + "\n");
console.log(`genext: highlighted ${functionNames.length} builtins and ${constantNames.length} constants in syntaxes/hphp.tmLanguage.json`);
