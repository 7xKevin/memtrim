(function () {
  var storageKey = "memtrim-theme";
  var root = document.documentElement;

  function applyTheme(theme) {
    root.setAttribute("data-theme", theme);
    var buttons = document.querySelectorAll("[data-theme-toggle]");
    buttons.forEach(function (button) {
      button.textContent = theme === "dark" ? "Light theme" : "Dark theme";
    });
  }

  var saved = localStorage.getItem(storageKey);
  applyTheme(saved === "dark" ? "dark" : "light");

  document.addEventListener("click", function (event) {
    var button = event.target.closest("[data-theme-toggle]");
    if (!button) {
      return;
    }

    var nextTheme = root.getAttribute("data-theme") === "dark" ? "light" : "dark";
    localStorage.setItem(storageKey, nextTheme);
    applyTheme(nextTheme);
  });
})();
