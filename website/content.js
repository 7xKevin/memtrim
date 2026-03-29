async function loadContent() {
  const response = await fetch("/data/site-content.json", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("Unable to load site content.");
  }
  return response.json();
}

function setText(id, value) {
  const element = document.getElementById(id);
  if (element && typeof value === "string") {
    element.textContent = value;
  }
}

function setLink(id, value) {
  const element = document.getElementById(id);
  if (element && typeof value === "string") {
    element.href = value;
  }
}

function renderList(id, items) {
  const element = document.getElementById(id);
  if (!element || !Array.isArray(items)) {
    return;
  }
  element.innerHTML = "";
  items.forEach((item) => {
    const li = document.createElement("li");
    li.textContent = item;
    element.appendChild(li);
  });
}

function renderDownloadButtons(downloads) {
  if (!Array.isArray(downloads) || downloads.length < 2) {
    return;
  }
  setText("download-primary-text", downloads[0].label);
  setLink("download-primary", downloads[0].url);
  setText("download-secondary-text", downloads[1].label);
  setLink("download-secondary", downloads[1].url);
}

function renderDownloadTable(rows) {
  const tbody = document.getElementById("download-table-body");
  if (!tbody || !Array.isArray(rows)) {
    return;
  }

  tbody.innerHTML = "";
  rows.forEach((row) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td><a href="${row.url}">${row.file}</a></td>
      <td>${row.description}</td>
    `;
    tbody.appendChild(tr);
  });
}

function renderSupport(content) {
  setText("support-title", content.support.title);
  setText("support-paragraph-1", content.support.paragraphs[0]);
  setLink("support-repo-link", content.repoUrl);
  setText("support-repo-link", content.repoUrl.replace(/^https?:\/\//, ""));
  setText("support-paragraph-2", content.support.paragraphs[1]);
  setText("support-paragraph-3", content.support.paragraphs[2]);
}

function renderPrivacy(content) {
  setText("privacy-title", content.privacy.title);
  setText("privacy-paragraph-1", content.privacy.paragraphs[0]);
  setText("privacy-paragraph-2", content.privacy.paragraphs[1]);
  setText("privacy-paragraph-3", content.privacy.paragraphs[2]);
}

function renderDevelopers(content) {
  setText("developers-title", content.developers.title);
  const tbody = document.getElementById("developers-table-body");
  if (!tbody || !Array.isArray(content.developers.members)) {
    return;
  }

  tbody.innerHTML = "";
  content.developers.members.forEach((member) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${member.name}</td><td>${member.role}</td>`;
    tbody.appendChild(tr);
  });
}

function renderChangelog(content) {
  const host = document.getElementById("changelog-sections");
  if (!host || !Array.isArray(content.changelog)) {
    return;
  }

  host.innerHTML = "";
  content.changelog.forEach((release) => {
    const section = document.createElement("section");
    section.className = "panel";
    const items = release.notes.map((note) => `<li>${note}</li>`).join("");
    section.innerHTML = `<h3>Version ${release.version}</h3><ul>${items}</ul>`;
    host.appendChild(section);
  });
}

function renderHome(content) {
  setText("brand-name", content.siteName);
  setText("brand-tagline", content.tagline);
  setText("hero-title", content.home.heroTitle);
  setText("hero-body", content.home.heroBody);
  setText("current-version", content.version.current);
  setText("version-summary", content.version.summary);
  renderDownloadButtons(content.downloads);
  renderList("home-main-features", content.home.mainFeatures);
  renderList("home-reasons", content.home.reasons);
  renderList("home-install-steps", content.home.installSteps);
  renderDownloadTable(content.downloadTable);
}

function applySharedContent(content) {
  document.querySelectorAll("[data-site-name]").forEach((node) => {
    node.textContent = content.siteName;
  });
  document.querySelectorAll("[data-tagline]").forEach((node) => {
    node.textContent = content.tagline;
  });
}

async function main() {
  try {
    const content = await loadContent();
    applySharedContent(content);

    switch (document.body.dataset.page) {
      case "home":
        renderHome(content);
        break;
      case "changelog":
        renderChangelog(content);
        break;
      case "privacy":
        renderPrivacy(content);
        break;
      case "support":
        renderSupport(content);
        break;
      case "developers":
        renderDevelopers(content);
        break;
      default:
        break;
    }
  } finally {
    document.body.classList.remove("is-loading");
  }
}

main().catch((error) => {
  console.error(error);
  document.body.classList.remove("is-loading");
});
