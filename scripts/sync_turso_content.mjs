import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createClient } from "@libsql/client";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const root = path.resolve(__dirname, "..");
const websiteDir = path.join(root, "website");
const dataDir = path.join(websiteDir, "data");
const defaultContentPath = path.join(dataDir, "default-site-content.json");
const outputContentPath = path.join(dataDir, "site-content.json");
const updateManifestPath = path.join(websiteDir, "update.json");

async function readJson(filePath) {
  const text = await readFile(filePath, "utf8");
  return JSON.parse(text);
}

function buildUpdateManifest(content) {
  const installer = content.downloadTable.find((item) => item.file.includes("Setup"))?.url
    ?? "/downloads/MemTrimLite-Setup.exe";
  const portable = content.downloadTable.find((item) => item.file.includes("Portable"))?.url
    ?? "/downloads/MemTrimLite-Portable.zip";

  return {
    version: content.version.current,
    installer_url: installer.startsWith("http")
      ? installer
      : `https://raw.githubusercontent.com/7xKevin/memtrim/main/website${installer}`,
    portable_url: portable.startsWith("http")
      ? portable
      : `https://raw.githubusercontent.com/7xKevin/memtrim/main/website${portable}`
  };
}

async function loadContentFromTurso(fallback) {
  const url = process.env.TURSO_DATABASE_URL;
  const authToken = process.env.TURSO_AUTH_TOKEN;

  if (!url || !authToken) {
    return fallback;
  }

  const client = createClient({ url, authToken });
  const result = await client.execute(
    "SELECT slug, json FROM site_documents WHERE slug IN ('site-content', 'update-manifest')"
  );

  const docs = Object.fromEntries(
    result.rows.map((row) => [row.slug, JSON.parse(row.json)])
  );

  const content = docs["site-content"] ?? fallback;
  const updateManifest = docs["update-manifest"] ?? buildUpdateManifest(content);
  return { content, updateManifest };
}

async function main() {
  await mkdir(dataDir, { recursive: true });

  const fallbackContent = await readJson(defaultContentPath);
  const loaded = await loadContentFromTurso(fallbackContent);
  const content = loaded.content ?? fallbackContent;
  const updateManifest = loaded.updateManifest ?? buildUpdateManifest(content);

  await writeFile(outputContentPath, `${JSON.stringify(content, null, 2)}\n`, "utf8");
  await writeFile(updateManifestPath, `${JSON.stringify(updateManifest, null, 2)}\n`, "utf8");

  console.log(`Wrote ${path.relative(root, outputContentPath)} and ${path.relative(root, updateManifestPath)}`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
