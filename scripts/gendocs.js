#!/usr/bin/env node
/* gendocs.js — generates docs/builtins.html from extension/hphp/data/builtins.json
 * (which scripts/genext.js generated from the compiler's own builtin table +
 * the hand-written explanations in scripts/builtin-docs.js).
 * Run after adding builtins:  node scripts/genext.js && node scripts/gendocs.js
 */
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const data = JSON.parse(
  fs.readFileSync(path.join(root, "extension/hphp/data/builtins.json"), "utf8")
);
const builtins = data.builtins || [];

/* ---- categories, in display order, with intro text ---- */
const CATS = [
  ["Strings", /^str|chr|ord|trim|ucfirst|lcfirst|nl2br|sprintf|printf|vsprintf|wordwrap|similar|levenshtein|number_format|html|url|bin2hex|hex2bin|join|implode|explode|addslashes|stripslashes|soundex|metaphone|nl2|quotemeta|str/,
   "Everything around byte strings: searching, cutting, comparing, padding, formatting and escaping. Strings are byte-oriented (UTF-8 text passes through untouched, but length/count are bytes)."],
  ["Regex", /^preg_/,
   "PCRE-style pattern matching on the built-in backtracking engine. Patterns use /.../ delimiters with trailing flags i, s, m; \\d \\w \\s classes, captures and alternation are supported. A bad pattern returns false, it does not throw."],
  ["Arrays", /^array_|in_array|sort|rsort|usort|ksort|asort|krsort|arsort|shuffle|range|compact|extract|count|sizeof|array$|key|current|next|prev|reset|end|each|list|push|pop|shift|splice|slice|merge|map|filter|reduce|walk|flip|fill|search|diff|intersect|sum|product|reverse|unique|column|combine|chunk|pad|keys|values/,
   "One array type covers lists and maps (string keys allowed). The in-place sorts (sort, ksort, usort, …) modify the array and return true. Closures passed to map/filter/reduce/walk/usort are normal fn() closures."],
  ["Math", /^(abs|ceil|floor|round|sqrt|pow|intdiv|fmod|log|log2|log10|exp|sin|cos|tan|asin|acos|atan|pi|deg2rad|rad2deg|min|max|rand|mt_rand|mt_srand|srand|is_nan|is_finite|is_infinite|hypot|lcg_value|bcadd|bcsub|bcdiv|bcmul|number)/,
   "Arithmetic helpers, trigonometry (radians!), rounding with PHP_ROUND_HALF_* modes, and randomness. rand()/mt_rand() are fine for games and shuffling; use random_int()/random_bytes() for anything security-related."],
  ["Types & vars", /^(is_|settype|gettype|intval|floatval|strval|boolval|empty|isset|unset|var_dump|print_r|var_export|serialize|unserialize|json_|define|defined|constant|get_class|method_exists|property_exists|call_user_func|func_get_args|func_num_args|type_of|typeof)/,
   "Introspection and conversion. var_dump() is your debugging friend. The compiler also checks types statically — these functions matter at runtime when values arrive untyped (JSON, user input, mixed params)."],
  ["Time", /^(time|microtime|hrtime|sleep|usleep|sleep_ms|date|mktime|strtotime|checkdate|strftime|getdate|localtime|gmdate|exit|die)/,
   "Unix timestamps are the base (int seconds since 1970). date() formats them, strtotime()/mktime() build them, hrtime() measures with nanosecond precision for benchmarks. exit/die live here because they end the program like a clock would end a party."],
  ["System & I/O", /^(file|fopen|fwrite|fread|fclose|fgets|feof|fgetc|file_get_contents|file_put_contents|unlink|rename|copy|mkdir|rmdir|is_dir|is_file|is_readable|is_writable|filesize|scandir|opendir|readdir|exec|system|shell_exec|passthru|proc_|readline|stream_|php_sapi|phpversion|php_uname|php_|getenv|putenv|memory_get|gc_|sys_|bindec|octdec|hexdec|getcwd|touch|realpath|dirname|basename|pathinfo)/,
   "Files (whole-file helpers plus fopen-style streaming), directories, process environment and external commands. The stream_* family is the raw TCP layer the registry server and WebSocket library are built on."],
  ["HTTP & MySQL", /^http_|parse_url|mysql_/,
   "A small HTTP client (http:// only — no TLS in the runtime) and a native MySQL/MariaDB wire-protocol client. http_get/post return [\"status\", \"body\", \"contentType\"] maps or false on failure; mysql_query returns rows as maps."],
  ["Hash & encode", /^(md5|sha1|sha256|crc32|hash_|password_|base64_|urlencode|urldecode|rawurlencode|rawurldecode|random_bytes|random_int|crypt)/,
   "Checksums (crc32, md5, sha1 — legacy), real hashes (sha256), keyed signatures (hash_hmac) and password storage (password_hash/verify, PBKDF2-SHA256). Rule of thumb: password_hash for logins, sha256 for everything else, hash_equals for comparing secrets."],
  ["UI (desktop)", /^ui_/,
   "Native desktop windows (Win32) with buttons, inputs, lists, menus, timers and dialogs — no external dependencies. Build the UI, register ui_on() callbacks, then call ui_run_main() to enter the event loop. Control kinds for ui_add(): 1 window, 2 button, 3 label, 4 input, 5 checkbox, 6 list, 7 combo, 8 progress, 9 slider, 10 group, 11 tab, 12 panel, 13 menu, 14 menuitem, 15 picture."],
  ["Unsafe / FFI", /^(free|memcpy|memset|ffi_|heap_dump)/,
   "Escape hatches to raw memory and native shared libraries. Every one of these requires an unsafe { } block in the compiler — with intent: this is where memory bugs live."],
];

function catOf(name) {
  for (const [cat, re] of CATS) if (re.test(name)) return cat;
  return "Other";
}

const groups = new Map();
for (const b of builtins) {
  const c = catOf(b.name);
  if (!groups.has(c)) groups.set(c, []);
  groups.get(c).push(b);
}
/* keep the declared order, append unknown groups */
const ordered = [...CATS.map(([c]) => c), "Other"].filter(c => groups.has(c));

const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

const rows = cat => {
  let h = "";
  for (const b of groups.get(cat)) {
    const sigHtml = esc(b.sig)
      .replace(new RegExp("^(" + b.name + ")"), '<span class="n">$1</span>')
      .replace(/:\s*(\S+)$/, ': <span class="r">$1</span>');
    const unsafe = b.unsafe ? ' <span class="badge" title="requires an unsafe block">unsafe</span>' : "";
    h += `<div class="bi-row" id="fn-${b.name}" data-name="${b.name}"><div class="sig">${sigHtml}${unsafe}</div><div class="d">${esc(b.desc || "")}</div></div>\n`;
  }
  return h;
};

let body = "";
for (const cat of ordered) {
  const intro = (CATS.find(([c]) => c === cat) || [])[2];
  body += `<h2 class="bi-group" id="cat-${cat.toLowerCase().replace(/[^a-z]+/g, "-")}">${cat} <span class="muted small">(${groups.get(cat).length})</span></h2>\n`;
  if (intro) body += `<p class="bi-intro">${intro}</p>\n`;
  body += rows(cat) + "\n";
}

/* predefined constants (solved by sema/codegen, not callable) */
const CONSTS = [
  ["PHP_INT_SIZE", "int", "8 — bytes per int on this build."],
  ["PHP_INT_MAX", "int", "9223372036854775807 — largest integer."],
  ["PHP_INT_MIN", "int", "-9223372036854775808 — smallest integer."],
  ["PHP_EOL", "string", "Platform line ending (\\r\\n on Windows)."],
  ["PHP_OS", "string", "Operating system family (\"Windows\")."],
  ["PHP_VERSION", "string", "HolyPHP runtime version."],
  ["PHP_PI / PHP_EULER / PHP_FLOAT_EPSILON", "float", "π, e, and the float epsilon."],
  ["PHP_ROUND_HALF_UP / _DOWN / _EVEN / _ODD", "int", "Mode flags for round()."],
  ["PHP_FILE_APPEND", "int", "Flag for file_put_contents(): append instead of truncate."],
  ["PHP_FILE_IGNORE_NEW_LINES / PHP_FILE_SKIP_EMPTY_LINES / PHP_FILE_USE_INCLUDE_PATH", "int", "Flags for file()/file() style helpers."],
  ["JSON_PRETTY_PRINT", "int", "json_encode() flag: multi-line indented output."],
  ["JSON_UNESCAPED_SLASHES", "int", "json_encode() flag: keep / unescaped."],
  ["JSON_NUMERIC_CHECK", "int", "json_encode() flag: numeric strings become numbers."],
  ["JSON_FORCE_OBJECT", "int", "json_encode() flag: empty arrays become {}."],
];
const constRows = CONSTS.map(([n, t, d]) =>
  `<div class="bi-row"><div class="sig"><span class="n">${n}</span> : <span class="r">${t}</span></div><div class="d">${d}</div></div>\n`).join("");

/* group nav */
const nav = ordered.map(cat => {
  const slug = cat.toLowerCase().replace(/[^a-z]+/g, "-");
  return `<a class="bi-nav-a" href="#cat-${slug}">${cat} <span class="muted">${groups.get(cat).length}</span></a>`;
}).join(" ");

const html = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Built-in functions — HolyPHP</title>
<link rel="stylesheet" href="assets/style.css">
<style>
/* fallback theme — mirrors assets/style.css so the page stays readable even
 * when the stylesheet fails to load (e.g. single-file preview servers) */
:root { --bg: #14151a; --bg2: #1b1c23; --bg3: #22242d; --fg: #e8e6e3; --dim: #9a97a3;
        --line: #2c2e38; --purple: #8892bf; --purple-hi: #aeb6e8; --ember: #ff7b3d;
        --green: #7ec699; --code-bg: #101116;
        --mono: "Cascadia Code", "JetBrains Mono", Consolas, Menlo, monospace;
        --sans: system-ui, "Segoe UI", sans-serif; }
body { margin: 0; background: var(--bg); color: var(--fg); font-family: var(--sans); line-height: 1.55; }
h1 { font-size: 30px; margin: 0 0 6px; }
h2 { font-size: 22px; margin: 42px 0 10px; padding-top: 14px; border-top: 1px solid var(--line); }
.lead { color: var(--dim); font-size: 17px; margin-bottom: 26px; }
.muted { color: var(--dim); }
.small { font-size: 12px; }
main { max-width: 980px; margin: 0 auto; padding: 28px 24px 80px; }
input[type="search"] { background: var(--bg2); color: var(--fg); border: 1px solid var(--line);
  border-radius: 8px; padding: 8px 12px; font-size: 14px; width: min(420px, 100%); margin-bottom: 8px; }
.bi-nav { margin: 14px 0 22px; display: flex; flex-wrap: wrap; gap: 4px 14px; }
.bi-nav-a { text-decoration: none; color: var(--purple-hi, #aeb6e8); opacity: .85; }
.bi-nav-a:hover { opacity: 1; text-decoration: underline; }
.bi-intro { margin: -6px 0 14px; max-width: 70ch; color: var(--dim); }
.bi-row { display: grid; grid-template-columns: minmax(240px, 42%) 1fr; gap: 4px 18px;
          padding: 7px 0; border-bottom: 1px solid var(--line); }
.bi-row .sig { font-family: var(--mono); font-size: 13.5px; word-break: break-word; }
.bi-row .sig .n { color: #61afef; font-weight: 600; }
.bi-row .sig .r { color: var(--green); }
.bi-row .d { color: var(--dim); font-size: 13px; margin-top: 2px; max-width: 68ch; }
.bi-row:target { background: rgba(255, 200, 0, .10); outline: 2px solid var(--ember); border-radius: 4px; }
.badge { font-size: .68rem; font-weight: 700; letter-spacing: .4px; text-transform: uppercase;
         color: #e06c75; border: 1px solid #e06c75; border-radius: 3px; padding: 0 4px; margin-left: 8px;
         vertical-align: 1px; }
@media (max-width: 700px) { .bi-row { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<div class="layout">
<script src="assets/nav.html.js"></script>
<main>
  <h1>Built-in <span class="ph">functions</span></h1>
  <p class="lead">${builtins.length} functions compiled into the language, generated from the
  compiler's own table and documented by hand. PHP names wherever PHP has them —
  if you know PHP, you already know this library.</p>

  <nav class="bi-nav">${nav}</nav>

  <input id="bi-search" type="search" placeholder="Filter functions…  (try: array, preg, password, ui_)" autocomplete="off">
  ${body}

  <h2 class="bi-group" id="cat-predefined-constants">Predefined constants <span class="muted small">(${CONSTS.length})</span></h2>
  <p class="bi-intro">Compile-time constants, usable anywhere a value is expected (not callable).</p>
  ${constRows}

  <p class="small muted" id="bi-none" style="display:none">No functions match.</p>
</main>
</div>
<script src="assets/site.js"></script>
<script>
const q = document.getElementById("bi-search");
q.addEventListener("input", () => {
  const s = q.value.toLowerCase();
  let any = false;
  document.querySelectorAll(".bi-row").forEach(r => {
    const hit = r.dataset.name.toLowerCase().includes(s) || r.textContent.toLowerCase().includes(s);
    r.style.display = hit ? "" : "none";
    if (hit) any = true;
  });
  document.querySelectorAll(".bi-group").forEach(h => {
    let vis = 0;
    let el = h.nextElementSibling;
    while (el && !el.classList.contains("bi-group")) {
      if (el.classList.contains("bi-row") && el.style.display !== "none") vis++;
      el = el.nextElementSibling;
    }
    h.style.display = vis ? "" : "none";
  });
  document.getElementById("bi-none").style.display = any ? "none" : "";
});
</script>
</body>
</html>
`;

fs.writeFileSync(path.join(root, "docs/builtins.html"), html);
console.log(`wrote docs/builtins.html with ${builtins.length} builtins in ${ordered.length} groups + ${CONSTS.length} constants`);
