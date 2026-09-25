#!/usr/bin/env node
/*
 * genext.js — generates extension/hphp/data/builtins.json from
 * src/hphpc/builtins.c (the compiler's own builtin table).
 *
 * Why generate: the extension's completions/hovers then always match the
 * compiler exactly. Run after adding builtins:  node scripts/genext.js
 */
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const builtinsC = fs.readFileSync(path.join(root, "src/hphpc/builtins.c"), "utf8");

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
};

/* pull the `add("name", SHAPE, ty_x, unsafe, variadic);` lines */
const re = /add\(\s*"([^"]+)"\s*,\s*(B_[A-Z0-9]+)\s*,\s*(ty_[a-z0-9]+)\s*,\s*(true|false)\s*,\s*(true|false)\s*\)/g;
const types = { ty_int: "int", ty_string: "string", ty_bool: "bool", ty_float: "float", ty_mixed: "mixed", ty_void: "void" };

let m, count = 0, entries = [];
while ((m = re.exec(builtinsC)) !== null) {
  const [, name, shape, ret, unsafe, variadic] = m;
  const sh = SHAPES[shape] || SHAPES.B_MISC;
  entries.push({
    name,
    sig: `${name}(${sh.params}): ${types[ret] || "mixed"}`,
    args: variadic === "true" ? -1 : sh.n,
    desc: sh.desc,
    unsafe: unsafe === "true",
  });
  count++;
}

const outDir = path.join(root, "extension", "hphp", "data");
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(
  path.join(outDir, "builtins.json"),
  JSON.stringify({ generatedFrom: "src/hphpc/builtins.c", builtins: entries }, null, 1)
);
console.log(`genext: wrote ${count} builtins to extension/hphp/data/builtins.json`);
if (count < 50) { console.error("genext: suspiciously few builtins — table regex mismatch?"); process.exit(1); }
