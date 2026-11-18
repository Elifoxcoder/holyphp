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

  // Copy only the code source. Reading pre.innerText would also copy the
  // button label and any other UI text placed inside the pre element.
  document.querySelectorAll("pre").forEach(pre => {
    if (document.body.classList.contains("playground-page") || pre.dataset.noCopy === "true" || pre.closest(".CodeMirror") || pre.querySelector(".copy-btn")) return;
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = "Copy code";
    btn.className = "copy-btn";
    btn.setAttribute("aria-label", "Copy code block");
    btn.addEventListener("click", () => {
      const code = pre.querySelector("code");
      const source = code ? code.textContent : pre.cloneNode(true).textContent;
      navigator.clipboard.writeText(source).then(() => {
        btn.textContent = "Copied";
        setTimeout(() => (btn.textContent = "Copy code"), 1400);
      }).catch(() => {
        btn.textContent = "Select and copy";
        setTimeout(() => (btn.textContent = "Copy code"), 1800);
      });
    });
    pre.style.position = "relative";
    pre.appendChild(btn);
  });

  // Guide navigation: track the current lesson and mark ordinary chapters.
  const guideSidebar = document.querySelector(".guide-sidebar");
  if (guideSidebar) {
    const lessonLinks = [...guideSidebar.querySelectorAll("[data-guide-section]")];
    const lessonSections = lessonLinks
      .map(link => document.getElementById(link.dataset.guideSection))
      .filter(Boolean);
    if (lessonSections.length) {
      const updateCurrentLesson = () => {
        const readingLine = window.innerHeight * 0.38;
        let index = 0;
        lessonSections.forEach((section, i) => {
          if (section.getBoundingClientRect().top <= readingLine) index = i;
        });
        lessonLinks.forEach((link, i) => {
          const active = i === index;
          link.classList.toggle("active", active);
          if (active) link.setAttribute("aria-current", "step");
          else link.removeAttribute("aria-current");
        });
      };
      window.addEventListener("scroll", updateCurrentLesson, { passive: true });
      window.addEventListener("resize", updateCurrentLesson);
      window.addEventListener("hashchange", () => setTimeout(updateCurrentLesson, 100));
      window.addEventListener("load", updateCurrentLesson, { once: true });
      updateCurrentLesson();
      requestAnimationFrame(() => requestAnimationFrame(updateCurrentLesson));
    } else {
      const current = guideSidebar.querySelector(`[href="${location.pathname.split("/").pop()}"]`);
      if (current) current.classList.add("active");
    }
  }

  // Reveal content as it enters view; users with reduced motion get the
  // normal static layout without transitions.
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const revealItems = document.querySelectorAll(".story-card, .capability, .hub-card, .safety-panel, .cta-band, .guide-step, .development-step");
  if (!reduceMotion && "IntersectionObserver" in window) {
    const revealObserver = new IntersectionObserver(entries => {
      entries.forEach(entry => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("is-visible");
        revealObserver.unobserve(entry.target);
      });
    }, { threshold: 0.08, rootMargin: "0px 0px -36px 0px" });
    revealItems.forEach((item, index) => {
      item.classList.add("reveal-on-scroll");
      item.style.transitionDelay = `${Math.min(index % 4, 3) * 55}ms`;
      revealObserver.observe(item);
    });
  }
});
