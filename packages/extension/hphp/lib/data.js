/* lib/data.js — the static language data the extension works from.
 *
 * Everything here is pure: no `vscode` import, so the test suite can load it
 * under plain node. `data/builtins.json` is generated from the compiler's own
 * table by scripts/genext.js; the constants mirror src/hphpc/codegen.c.
 */
"use strict";

const fs = require("fs");
const path = require("path");

/* ---------------- keywords ---------------- */

const KEYWORDS = [
  ["if", "control", "if (cond) { ... }"],
  ["else", "control", "else { ... }"],
  ["elseif", "control", "elseif (cond) { ... }"],
  ["while", "control", "while (cond) { ... }"],
  ["for", "control", "for (init; cond; step) { ... }"],
  ["foreach", "control", "foreach ($arr as $k => $v) { ... }"],
  ["as", "control", "foreach ($arr as $v)"],
  ["in", "control", "for ($x in $items) { ... }"],
  ["switch", "control", "switch ($x) { case $v: ... break; }"],
  ["case", "control", "case value:"],
  ["default", "control", "default:"],
  ["do", "control", "do { ... } while (cond);"],
  ["match", "control", "match ($x) { $pat => $val, default => $val }"],
  ["return", "control", "return $expr;"],
  ["break", "control", "break; / break 2;"],
  ["continue", "control", "continue;"],
  ["try", "control", "try { ... } catch ($e) { ... }"],
  ["catch", "control", "catch (Exception $e) { ... }"],
  ["finally", "control", "finally { ... }"],
  ["throw", "control", 'throw new Exception("msg");'],
  ["yield", "control", "yield $value;"],
  ["function", "declaration", "function name(int $p): int { ... }"],
  ["fn", "declaration", "fn($x) => $x + 1"],
  ["class", "declaration", "class Name { public int $f; }"],
  ["interface", "declaration", "interface Name { ... }"],
  ["trait", "declaration", "trait Name { ... }"],
  ["enum", "declaration", "enum Color { Red, Green }"],
  ["extends", "declaration", "class A extends B"],
  ["implements", "declaration", "class A implements I"],
  ["new", "declaration", "new Class($args)"],
  ["this", "declaration", "$this->field"],
  ["self", "declaration", "self::method()"],
  ["parent", "declaration", "parent::__construct()"],
  ["public", "modifier", "visibility modifier"],
  ["protected", "modifier", "visibility modifier"],
  ["private", "modifier", "visibility modifier"],
  ["static", "modifier", "static member"],
  ["abstract", "modifier", "abstract class/method"],
  ["final", "modifier", "final class/method"],
  ["const", "modifier", "const NAME = value;"],
  ["global", "modifier", "global $config;"],
  ["echo", "statement", 'echo "text";'],
  ["print", "statement", "print $expr;"],
  ["unsafe", "memory", "unsafe { *$ptr ... } — raw pointer block"],
  ["own", "memory", "own T — exclusive ownership"],
  ["mut", "memory", "mut — mutable binding/reference"],
  ["let", "memory", "let $x: int = ...; typed declaration"],
  ["use", "module", "use Name;"],
  ["import", "module", 'import "module";'],
  ["export", "module", "export function name(...) { ... }"],
];

const TYPES = [
  "int", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "isize",
  "float", "f32", "f64", "double", "bool", "string", "str", "array", "list",
  "mixed", "void", "Vec", "Map", "Set", "Option", "Result",
];

/* ---------------- predefined constants (codegen.c) ---------------- */

/* value: the JS value the compiler folds in; literal: how it is written in
 * source (what hover shows). String constants keep their quotes. */
const CONSTANTS = Object.create(null);

function defineConstant(name, value, type, desc, literal) {
  CONSTANTS[name] = {
    name, value, type, desc,
    literal: literal === undefined ? String(value) : literal,
  };
}

/* ANSI colours: 0..16, second argument of print()/println()/input(). */
[
  ["RESET", 0, "the terminal's default colour"],
  ["BLACK", 1, "black"], ["RED", 2, "red"], ["GREEN", 3, "green"],
  ["YELLOW", 4, "yellow"], ["BLUE", 5, "blue"], ["MAGENTA", 6, "magenta"],
  ["CYAN", 7, "cyan"], ["WHITE", 8, "white"],
  ["BRIGHT_BLACK", 9, "bright black (grey)"], ["BRIGHT_RED", 10, "bright red"],
  ["BRIGHT_GREEN", 11, "bright green"], ["BRIGHT_YELLOW", 12, "bright yellow"],
  ["BRIGHT_BLUE", 13, "bright blue"], ["BRIGHT_MAGENTA", 14, "bright magenta"],
  ["BRIGHT_CYAN", 15, "bright cyan"], ["BRIGHT_WHITE", 16, "bright white"],
].forEach(([name, value, colour]) => {
  defineConstant(name, value, "int",
    `ANSI colour ${value} (${colour}) — pass as the second argument of ` +
    `print() / println() / input().`);
});

defineConstant("PHP_INT_MAX", "9223372036854775807", "int", "Largest 64-bit signed integer.");
defineConstant("PHP_INT_MIN", "-9223372036854775808", "int", "Smallest 64-bit signed integer.");
defineConstant("PHP_INT_SIZE", 8, "int", "Size of an integer in bytes (always 8).");
defineConstant("PHP_INT_DIGITS", 19, "int", "Decimal digits that fit in an integer.");
defineConstant("PHP_FLOAT_EPSILON", 2.220446049250313e-16, "float", "Smallest representable float step.");
defineConstant("PHP_EULER", 2.718281828459045, "float", "Euler's number e.");
defineConstant("PHP_PI", 3.141592653589793, "float", "The constant π.");
defineConstant("M_PI", 3.141592653589793, "float", "The constant π (math alias).");
defineConstant("M_E", 2.718281828459045, "float", "Euler's number e (math alias).");
defineConstant("PHP_ROUND_HALF_UP", 0, "int", "round() mode: .5 away from zero.");
defineConstant("PHP_ROUND_HALF_DOWN", 1, "int", "round() mode: .5 towards zero.");
defineConstant("PHP_ROUND_HALF_EVEN", 2, "int", "round() mode: .5 to the even neighbour.");
defineConstant("PHP_ROUND_HALF_ODD", 3, "int", "round() mode: .5 to the odd neighbour.");
defineConstant("PHP_OS", "Windows", "string", "Operating system name.", '"Windows"');
defineConstant("PHP_EOL", "\r\n", "string", "End-of-line sequence of this platform.", '"\\r\\n"');
defineConstant("PHP_VERSION", null, "string", "Compiler version at run time (from the runtime).", "string");
defineConstant("PHP_SAPI", null, "string", "Server API name at run time.", "string");
defineConstant("PHP_UNAME", null, "string", "System description at run time.", "string");
defineConstant("PHP_TRUE", true, "bool", "Boolean true as a value.");
defineConstant("PHP_FALSE", false, "bool", "Boolean false as a value.");
defineConstant("PHP_NULL", null, "null", "The null value.", "null");
defineConstant("JSON_PRETTY_PRINT", 1, "int", "json_encode() flag: indent the output.");
defineConstant("JSON_UNESCAPED_SLASHES", 2, "int", "json_encode() flag: keep `/` unescaped.");
defineConstant("JSON_NUMERIC_CHECK", 4, "int", "json_encode() flag: numeric strings become numbers.");
defineConstant("JSON_FORCE_OBJECT", 8, "int", "json_encode() flag: arrays become objects.");
defineConstant("PHP_FILE_APPEND", 8, "int", "file_put_contents() flag: append instead of replace.");
defineConstant("PHP_FILE_IGNORE_NEW_LINES", 2, "int", "file() flag: strip trailing newlines.");
defineConstant("PHP_FILE_SKIP_EMPTY_LINES", 4, "int", "file() flag: drop empty lines.");
defineConstant("PHP_FILE_USE_INCLUDE_PATH", 1, "int", "file() flag: search the include path.");

/* ---------------- signature parsing ---------------- */

/* Split "a, b[c], ...d" on top-level commas (no nesting inside <>, [], ()). */
function splitParams(s) {
  const parts = [];
  let depth = 0, cur = "";
  for (const ch of s) {
    if (ch === "<" || ch === "[" || ch === "(") depth++;
    else if (ch === ">" || ch === "]" || ch === ")") depth--;
    if (ch === "," && depth === 0) { parts.push(cur); cur = ""; }
    else cur += ch;
  }
  if (cur.trim()) parts.push(cur);
  return parts;
}

/* "println(mixed $value[, int $colour]): void" ->
 *   [{ name: "$value", type: "mixed", optional: false, variadic: false },
 *    { name: "$colour", type: "int", optional: true, variadic: false }]
 *
 * PHP doc style marks the optional tail with brackets: `a[, b[, c]]`. */
function parseSignature(sig) {
  if (typeof sig !== "string") return [];
  const open = sig.indexOf("(");
  if (open < 0) return [];
  let depth = 0, close = -1;
  for (let i = open; i < sig.length; i++) {
    if (sig[i] === "(") depth++;
    else if (sig[i] === ")") { depth--; if (depth === 0) { close = i; break; } }
  }
  const inner = sig.slice(open + 1, close < 0 ? sig.length : close).trim();
  if (!inner) return [];

  const opt = inner.indexOf("[");
  const required = opt < 0 ? inner : inner.slice(0, opt);
  const optional = opt < 0 ? "" : inner.slice(opt).replace(/[\[\]]/g, "");

  const out = [];
  const push = (raw, isOptional) => {
    let part = raw.trim();
    if (!part) return;
    const variadic = /^\.\.\./.test(part);
    if (variadic) part = part.slice(3).trim();
    const nameMatch = /(\$[A-Za-z_]\w*)\s*$/.exec(part);
    const name = nameMatch ? nameMatch[1] : "";
    let type = nameMatch ? part.slice(0, nameMatch.index).trim() : part;
    if (!type) type = "mixed";
    out.push({ name, type, optional: isOptional, variadic });
  };
  for (const p of splitParams(required)) push(p, false);
  for (const p of splitParams(optional)) push(p, true);
  return out;
}

/* The return type of a signature: the part after the closing paren. */
function returnType(sig) {
  const m = /\)\s*:\s*(.+)$/.exec(sig || "");
  return m ? m[1].trim() : "";
}

/* ---------------- builtin table ---------------- */

let cache = null;

function load() {
  if (cache) return cache;
  const sourcePath = path.join(__dirname, "..", "data", "builtins.json");
  const bundledPath = path.join(__dirname, "data", "builtins.json");
  const file = fs.existsSync(sourcePath) ? sourcePath : bundledPath;
  let builtins = [];
  try {
    const raw = JSON.parse(fs.readFileSync(file, "utf8"));
    builtins = raw.builtins || [];
  } catch (e) {
    builtins = [];
  }
  const byName = new Map();
  for (const b of builtins) {
    const parsed = parseSignature(b.sig);
    if (b.params && b.params.length) {
      b.params = b.params.map((p, i) => ({ ...parsed[i], ...p,
        optional: p.optional === undefined ? !!(parsed[i] && parsed[i].optional) : p.optional,
        variadic: p.variadic === undefined ? !!(parsed[i] && parsed[i].variadic) : p.variadic,
      }));
    } else b.params = parsed;
    b.ret = returnType(b.sig);
    b.constant = Object.prototype.hasOwnProperty.call(CONSTANTS, b.name);
    byName.set(b.name, b);
  }
  cache = { builtins, byName, constants: CONSTANTS };
  return cache;
}

function builtin(name) {
  return load().byName.get(name) || null;
}

function isConstant(name) {
  return Object.prototype.hasOwnProperty.call(CONSTANTS, name);
}

function constant(name) {
  return isConstant(name) ? CONSTANTS[name] : null;
}

module.exports = {
  KEYWORDS,
  TYPES,
  CONSTANTS,
  load,
  builtin,
  isConstant,
  constant,
  parseSignature,
  returnType,
  splitParams,
};
