/* nav.html.js — injects the shared navigation into every docs page.
 * Desktop: sticky sidebar. Mobile (<880px): hamburger button + slide-in
 * drawer; the button is hidden while printing. */
document.write(`
<button class="nav-burger" id="nav-burger" aria-label="Open navigation"
        aria-expanded="false" aria-controls="side-nav">
  <span></span><span></span><span></span>
</button>
<div class="nav-scrim" id="nav-scrim" hidden></div>

<nav class="side" id="side-nav">
  <a class="logo" href="index.html">Holy<b>PHP</b></a>
  <span class="tag">compiled &#183; memory-safe &#183; PHP-easy</span>

  <h4>Start</h4>
  <a class="item" href="index.html">Overview</a>
  <a class="item" href="lang.html">Language reference</a>
  <a class="item" href="examples.html">Examples</a>

  <h4>Standard library</h4>
  <a class="item" href="lib-async.html">async &#8212; event loop &amp; HTTP</a>
  <a class="item" href="lib-thread.html">thread &#8212; concurrency</a>
  <a class="item" href="lib-ui.html">ui &#8212; desktop GUI</a>
  <a class="item" href="lib-websocket.html">websocket</a>
  <a class="item" href="builtins.html">Built-in functions</a>

  <h4>Toolchain</h4>
  <a class="item" href="cli.html">hphp command line</a>
  <a class="item" href="pkg.html">Packages &#8212; hphp pkg</a>
</nav>

<script>
(function() {
  var burger = document.getElementById("nav-burger");
  var nav = document.getElementById("side-nav");
  var scrim = document.getElementById("nav-scrim");
  if (!burger || !nav || !scrim) return;
  function setOpen(open) {
    nav.classList.toggle("open", open);
    scrim.hidden = !open;
    burger.setAttribute("aria-expanded", open ? "true" : "false");
    document.body.classList.toggle("nav-lock", open);
  }
  burger.addEventListener("click", function() {
    setOpen(!nav.classList.contains("open"));
  });
  scrim.addEventListener("click", function() { setOpen(false); });
  document.addEventListener("keydown", function(e) {
    if (e.key === "Escape") setOpen(false);
  });
  nav.addEventListener("click", function(e) {
    if (e.target && e.target.classList && e.target.classList.contains("item"))
      setOpen(false);
  });
})();
</script>
`);
