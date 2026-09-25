/* nav.html.js — injects the shared sidebar into every docs page */
document.write(`
<nav class="side">
  <a class="logo" href="index.html">Holy<b>PHP</b></a>
  <span class="tag">compiled &#183; memory-safe &#183; PHP-easy</span>

  <h4>Start</h4>
  <a class="item" href="index.html">Overview</a>
  <a class="item" href="lang.html">Language reference</a>
  <a class="item" href="examples.html">Examples</a>

  <h4>Standard library</h4>
  <a class="item" href="lib-ui.html">ui.hphp &#8212; desktop GUI</a>
  <a class="item" href="lib-websocket.html">websocket.hphp</a>
  <a class="item" href="builtins.html">Built-in functions</a>

  <h4>Toolchain</h4>
  <a class="item" href="cli.html">hphp command line</a>
  <a class="item" href="pkg.html">Packages — hphp pkg</a>
</nav>
`);
