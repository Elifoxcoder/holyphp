/* Shared top navigation plus a persistent chapter rail on both learning guides. */
var NAV_BASE = (function () {
  var script = document.currentScript;
  if (script && script.src) return script.src.replace(/assets\/nav\.html\.js.*$/, "");
  var path = location.href.split("#")[0].split("?")[0];
  path = path.slice(0, path.lastIndexOf("/") + 1);
  if (/\/guide\/$/.test(path)) path = path.slice(0, -"guide/".length);
  return path;
})();

var NAV_PATH = location.pathname;
/* which top-level section the page belongs to (highlights the nav link) */
var NAV_SECTION = /\/libraries\//.test(NAV_PATH) ? "libraries"
  : /\/guide(\/|\.html)/.test(NAV_PATH) ? "tutorial"
  : /\/(docs|lang|builtins|cli|pkg)\.html$|\/builtin\//.test(NAV_PATH) ? "docs"
  : /\/(index\.html)?$/.test(NAV_PATH) ? "home" : "";
var INTERACTIVE_GUIDE = /\/guide\.html$/.test(NAV_PATH);
var BEGINNER_GUIDE = /\/guide\/(?:[^/]+\.html)?$/.test(NAV_PATH);
var GUIDE_RAIL = "";

if (INTERACTIVE_GUIDE) {
  var interactiveLessons = [
    ["step1", "Hello, World!", "Your first program"],
    ["step2", "Variables & types", "Store and describe values"],
    ["step3", "Functions", "Reuse small pieces of code"],
    ["step4", "Decisions & loops", "Choose and repeat actions"],
    ["step5", "Arrays & maps", "Work with collections"],
    ["step6", "Classes", "Group data and behavior"],
    ["step7", "Memory safety", "References and ownership"],
    ["step8", "Build an application", "Libraries and next steps"]
  ];
  GUIDE_RAIL = `<aside class="guide-sidebar" aria-label="Guide chapters">
    <div class="guide-rail-heading"><span class="section-kicker">Learning path</span><strong>Learn HolyPHP</strong><span>${interactiveLessons.length} interactive lessons</span></div>
    <nav class="guide-steps" aria-label="Lessons">${interactiveLessons.map((item, i) => `<a class="guide-step-link" href="#${item[0]}" data-guide-section="${item[0]}"><span class="guide-step-index">${String(i + 1).padStart(2, "0")}</span><span class="guide-step-copy"><b>${item[1]}</b><small>${item[2]}</small></span></a>`).join("")}</nav>
    <div class="guide-rail-foot"><a href="${NAV_BASE}docs.html" target="_blank" rel="noopener">Open the full docs ↗</a><p>Go at your own pace. Each lesson has an example and an exercise.</p></div>
  </aside>`;
} else if (BEGINNER_GUIDE) {
  var beginnerChapters = [
    ["01-what-is-holyphp.html", "What is HolyPHP?"],
    ["02-installation.html", "Install the compiler"],
    ["03-hello-world.html", "Your first program"],
    ["04-using-the-playground.html", "Use the Playground"],
    ["05-what-are-variables.html", "Variables"],
    ["06-data-types.html", "Data types"],
    ["07-operators.html", "Operators"],
    ["08-input-and-output.html", "Input and output"],
    ["09-comments.html", "Comments"]
  ];
  GUIDE_RAIL = `<aside class="guide-sidebar beginner-sidebar" aria-label="Beginner guide chapters">
    <div class="guide-rail-heading"><span class="section-kicker">Beginner guide</span><strong>Start with the basics</strong><span>9 short chapters</span></div>
    <nav class="guide-steps" aria-label="Chapters"><a class="guide-step-link guide-overview-link" href="${NAV_BASE}guide/index.html"><span class="guide-step-index">↗</span><span class="guide-step-copy"><b>Guide overview</b><small>See the full learning path</small></span></a>${beginnerChapters.map((item, i) => `<a class="guide-step-link${NAV_PATH.endsWith(item[0]) ? " active" : ""}" href="${NAV_BASE}guide/${item[0]}"><span class="guide-step-index">${String(i + 1).padStart(2, "0")}</span><span class="guide-step-copy"><b>${item[1]}</b><small>Chapter ${i + 1}</small></span></a>`).join("")}</nav>
    <div class="guide-rail-foot"><a href="${NAV_BASE}guide.html">Continue to practice →</a><p>Learn a concept, try the example, then solve the exercise.</p></div>
  </aside>`;
}

document.write(`
<header class="site-header">
  <div class="site-nav">
    <a class="site-brand" href="${NAV_BASE}index.html" aria-label="HolyPHP home"><span class="brand-mark">H</span><span>Holy<span>PHP</span></span></a>
    <button class="nav-burger" id="nav-burger" aria-label="Open navigation" aria-expanded="false" aria-controls="site-links"><span></span><span></span></button>
    <nav class="site-links" id="site-links" aria-label="Main navigation">
      <a href="${NAV_BASE}index.html"${NAV_SECTION === "home" ? ' class="active"' : ""}>Overview</a>
      <a href="${NAV_BASE}guide/index.html"${NAV_SECTION === "tutorial" ? ' class="active"' : ""}>Tutorial</a>
      <a href="${NAV_BASE}docs.html"${NAV_SECTION === "docs" ? ' class="active"' : ""}>Documentation</a>
      <a href="${NAV_BASE}libraries/"${NAV_SECTION === "libraries" ? ' class="active"' : ""}>Libraries</a>
      <a href="${NAV_BASE}playground.html" target="_blank" rel="noopener">Playground <span aria-hidden="true">↗</span></a>
      <a class="github-link" href="https://github.com/Elifoxcoder/holyphp" target="_blank" rel="noopener">GitHub <span aria-hidden="true">↗</span></a>
    </nav>
    <button class="theme-toggle" type="button" data-theme-toggle aria-label="Switch to dark mode"><span class="theme-toggle-icon" aria-hidden="true">☾</span><span class="theme-toggle-label" data-theme-label>Dark mode</span></button>
  </div>
</header>
${GUIDE_RAIL}
<script>
(function() {
  if (document.querySelector('.guide-sidebar')) document.body.classList.add('has-guide-sidebar');
  var button = document.getElementById('nav-burger');
  var links = document.getElementById('site-links');
  if (!button || !links) return;
  function setOpen(open) {
    button.setAttribute('aria-expanded', open ? 'true' : 'false');
    button.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation');
    links.classList.toggle('is-open', open);
    document.body.classList.toggle('nav-lock', open);   /* fullscreen menu: no page scroll */
  }
  button.addEventListener('click', function() {
    setOpen(button.getAttribute('aria-expanded') !== 'true');
  });
  links.addEventListener('click', function(event) {
    if (event.target.closest('a')) setOpen(false);
  });
  document.addEventListener('keydown', function(event) {
    if (event.key === 'Escape' && links.classList.contains('is-open')) { setOpen(false); button.focus(); }
  });
  window.addEventListener('resize', function() {
    if (window.innerWidth > 900 && links.classList.contains('is-open')) setOpen(false);
  });
})();
</script>
`);
