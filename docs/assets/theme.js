/* Persistent theme toggle shared by the website, playground and built-in docs. */
(function () {
  const STORAGE_KEY = "hphp-theme";
  const readTheme = () => {
    try { return localStorage.getItem(STORAGE_KEY); } catch (_) { return null; }
  };
  const writeTheme = value => {
    try { localStorage.setItem(STORAGE_KEY, value); } catch (_) { /* storage may be disabled */ }
  };

  const initialTheme = readTheme();
  document.documentElement.dataset.theme = initialTheme === "dark" ? "dark" : "light";

  function updateButtons() {
    const dark = document.documentElement.dataset.theme === "dark";
    document.querySelectorAll("[data-theme-toggle]").forEach(button => {
      const label = button.querySelector("[data-theme-label]");
      const icon = button.querySelector(".theme-toggle-icon");
      if (label) label.textContent = dark ? "Light mode" : "Dark mode";
      if (icon) icon.textContent = dark ? "☀" : "☾";
      button.setAttribute("aria-label", dark ? "Switch to light mode" : "Switch to dark mode");
      button.setAttribute("aria-pressed", dark ? "true" : "false");
      button.title = dark ? "Switch to light mode" : "Switch to dark mode";
    });
  }

  function setTheme(value) {
    const nextTheme = value === "dark" ? "dark" : "light";
    document.documentElement.dataset.theme = nextTheme;
    writeTheme(nextTheme);
    updateButtons();
  }

  function init() {
    if (!document.querySelector("[data-theme-toggle]")) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "theme-toggle theme-toggle-floating";
      button.dataset.themeToggle = "";
      button.innerHTML = '<span class="theme-toggle-icon" aria-hidden="true">☾</span><span class="theme-toggle-label" data-theme-label>Dark mode</span>';
      document.body.appendChild(button);
    }
    updateButtons();
    document.querySelectorAll("[data-theme-toggle]").forEach(button => {
      button.addEventListener("click", () => {
        setTheme(document.documentElement.dataset.theme === "dark" ? "light" : "dark");
      });
    });
  }

  window.addEventListener("storage", event => {
    if (event.key === STORAGE_KEY && event.newValue) setTheme(event.newValue);
  });
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init, { once: true });
  else init();
})();
