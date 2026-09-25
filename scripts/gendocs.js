#!/usr/bin/env node
/* gendocs.js — generates docs/builtins.html from extension/hphp/data/builtins.json
 * (which scripts/genext.js generated from the compiler's own builtin table).
 * Run after adding builtins:  node scripts/genext.js && node scripts/gendocs.js
 */
const fs = require("fs");
const path = require("path");

const root = path.join(__dirname, "..");
const data = JSON.parse(
  fs.readFileSync(path.join(root, "extension/hphp/data/builtins.json"), "utf8")
);
const builtins = data.builtins || [];

/* ---- categorize by name prefix ---- */
const CATS = [
  ["Strings", /^str|chr|ord|trim|ucfirst|lcfirst|nl2br|sprintf|printf|vsprintf|wordwrap|similar|levenshtein|number_format|html|url|bin2hex|hex2bin|join|implode|explode|addslashes|stripslashes|soundex|metaphone|nl2|quotemeta|str/],
  ["Arrays", /^array_|in_array|sort|rsort|usort|ksort|asort|krsort|arsort|shuffle|range|compact|extract|count|sizeof|array$|key|current|next|prev|reset|end|each|list|push|pop|shift|splice|slice|merge|map|filter|reduce|walk|flip|fill|search|diff|intersect|sum|product|reverse|unique|column|combine|chunk|pad|keys|values/],
  ["Math", /^(abs|ceil|floor|round|sqrt|pow|intdiv|fmod|log|log2|log10|exp|sin|cos|tan|asin|acos|atan|pi|deg2rad|rad2deg|min|max|rand|mt_rand|mt_srand|srand|is_nan|is_finite|is_infinite|hypot|bcadd|bcsub|bcdiv|bcmul|number)/],
  ["Types & vars", /^(is_|settype|gettype|intval|floatval|strval|boolval|empty|isset|unset|var_dump|print_r|serialize|unserialize|json_|define|defined|constant|get_class|method_exists|property_exists|call_user_func|func_get_args|func_num_args|type_of|typeof)/],
  ["Time", /^(time|microtime|hrtime|sleep|usleep|sleep_ms|date|mktime|strtotime|checkdate|strftime|getdate|localtime|gmdate)/],
  ["System & I/O", /^(file|fopen|fwrite|fread|fclose|fgets|file_get_contents|file_put_contents|unlink|rename|copy|mkdir|rmdir|is_dir|is_file|file_exists|filesize| scandir|opendir|readdir|exec|system|shell_exec|passthru|proc_|readline|stream_|php_sapi|phpversion|php_uname|php_|getenv|putenv|memory_get|gc_)/],
  ["Hash & encode", /^(md5|sha1|crc32|hash|base64_|urlencode|urldecode|rawurlencode|rawurldecode|password_|crypt|random_bytes|random_int)/],
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

const rows = cat => {
  let h = "";
  for (const b of groups.get(cat)) {
    const ret = (b.sig.match(/:\s*([^()]+)$/i) || [, ""])[1].trim();
    const sigHtml = b.sig
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(new RegExp("^(" + b.name + ")"), '<span class="n">$1</span>')
      .replace(new RegExp(":\\s*(\\S+)$"), ': <span class="r">$1</span>');
    h += `<div class="bi-row" data-name="${b.name}"><div class="sig">${sigHtml}</div><div class="d">${b.desc || ""}</div></div>\n`;
  }
  return h;
};

let body = "";
for (const [cat] of groups) {
  body += `<h2 class="bi-group">${cat} <span class="muted small">(${groups.get(cat).length})</span></h2>\n${rows(cat)}\n`;
}

const html = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Built-in functions — HolyPHP</title>
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<div class="layout">
<script src="assets/nav.html.js"></script>
<main>
  <h1>Built-in <span class="ph">functions</span></h1>
  <p class="lead">${builtins.length} functions compiled into the language, generated from the
  compiler's own table. PHP names wherever PHP has them.</p>
  <input id="bi-search" type="search" placeholder="Filter functions…  (try: array, str, time)" autocomplete="off">
  ${body}
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
console.log(`wrote docs/builtins.html with ${builtins.length} builtins in ${groups.size} groups`);
