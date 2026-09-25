/* site.js — shared behavior: nav highlight, hphp syntax highlighting, copy buttons */

/* ---------- tiny hphp highlighter ---------- */
const HPHP_KEYWORDS = new Set((
  "function pub private public return if else elseif while for foreach as break continue " +
  "switch case default match enum class extends implements interface new try catch finally " +
  "throw use fn echo print import unsafe own Rc static const this null true false and or xor " +
  "let mut int float bool string array mixed void exit die global include require"
).split(/\s+/));

const HPHP_TYPES = new Set(("int float bool string array mixed void own Rc").split(/\s+/));

function hl_hphp(src) {
  let out = "",
    i = 0,
    n = src.length;
  const esc = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  while (i < n) {
    const c = src[i];
    // comments
    if ((c === "/" && src[i + 1] === "/") || c === "#") {
      let j = src.indexOf("\n", i);
      if (j < 0) j = n;
      out += '<span class="tok-com">' + esc(src.slice(i, j)) + "</span>";
      i = j;
      continue;
    }
    if (c === "/" && src[i + 1] === "*") {
      let j = src.indexOf("*/", i + 2);
      j = j < 0 ? n : j + 2;
      out += '<span class="tok-com">' + esc(src.slice(i, j)) + "</span>";
      i = j;
      continue;
    }
    // strings (incl. interpolation — keep it simple, highlight as string)
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < n && src[j] !== c) {
        if (src[j] === "\\") j++;
        j++;
      }
      j = Math.min(j + 1, n);
      out += '<span class="tok-str">' + esc(src.slice(i, j)) + "</span>";
      i = j;
      continue;
    }
    // variables
    if (c === "$") {
      let j = i + 1;
      while (j < n && /[A-Za-z0-9_]/.test(src[j])) j++;
      out += '<span class="tok-var">' + esc(src.slice(i, j)) + "</span>";
      i = j;
      continue;
    }
    // numbers
    if (/[0-9]/.test(c)) {
      let j = i;
      while (j < n && /[0-9a-fA-FxX._]/.test(src[j])) j++;
      out += '<span class="tok-num">' + esc(src.slice(i, j)) + "</span>";
      i = j;
      continue;
    }
    // identifiers / keywords
    if (/[A-Za-z_]/.test(c)) {
      let j = i;
      while (j < n && /[A-Za-z0-9_]/.test(src[j])) j++;
      const w = src.slice(i, j);
      let k = j;
      while (k < n && /\s/.test(src[k])) k++;
      const isCall = src[k] === "(";
      if (HPHP_KEYWORDS.has(w)) out += '<span class="tok-kw">' + w + "</span>";
      else if (HPHP_TYPES.has(w)) out += '<span class="tok-type">' + w + "</span>";
      else if (isCall) out += '<span class="tok-fn">' + w + "</span>";
      else if (/^[A-Z]/.test(w)) out += '<span class="tok-cls">' + w + "</span>";
      else out += esc(w);
      i = j;
      continue;
    }
    if (/[=+\-*/.!<>?:&|%^]/.test(c)) {
      out += '<span class="tok-op">' + esc(c) + "</span>";
      i++;
      continue;
    }
    out += esc(c);
    i++;
  }
  return out;
}

/* ---------- render on load ---------- */
document.addEventListener("DOMContentLoaded", () => {
  // highlight all <pre><code class="hphp"> blocks
  document.querySelectorAll('pre code.hphp, pre code[class="language-hphp"]').forEach(el => {
    el.innerHTML = hl_hphp(el.textContent);
  });

  // copy button on every pre
  document.querySelectorAll("pre").forEach(pre => {
    const btn = document.createElement("button");
    btn.textContent = "copy";
    btn.className = "copy-btn";
    btn.addEventListener("click", () => {
      navigator.clipboard.writeText(pre.innerText).then(() => {
        btn.textContent = "ok!";
        setTimeout(() => (btn.textContent = "copy"), 1200);
      });
    });
    pre.style.position = "relative";
    btn.style.cssText =
      "position:absolute;top:8px;right:8px;background:#22242d;color:#9a97a3;border:1px solid #2c2e38;" +
      "border-radius:5px;font-size:11px;padding:2px 9px;cursor:pointer;opacity:0;transition:opacity .15s";
    pre.addEventListener("mouseenter", () => (btn.style.opacity = 1));
    pre.addEventListener("mouseleave", () => (btn.style.opacity = 0));
    pre.appendChild(btn);
  });

  // current-page nav highlight
  const here = location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll("nav.side a.item").forEach(a => {
    const t = a.getAttribute("href");
    if (t === here || (here === "" && t === "index.html")) a.classList.add("on");
  });
});