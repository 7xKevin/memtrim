# Turso Setup

MemTrim Lite now supports a Turso-backed static content export flow while staying compatible with the current Render Blueprint static-site deployment.

## What happens

1. Render runs `npm install && npm run build:site`.
2. `scripts/sync_turso_content.mjs` reads Turso content if credentials are present.
3. It writes:
   - `website/data/site-content.json`
   - `website/update.json`
4. The static website reads `site-content.json`.
5. The Windows app keeps reading `update.json`.

If Turso credentials are missing, the build falls back to `website/data/default-site-content.json`.

## Required Render environment variables

- `TURSO_DATABASE_URL`
- `TURSO_AUTH_TOKEN`

Add them to the Render service settings or keep them declared through the Blueprint env vars in `render.yaml`.

## Turso schema

Run the SQL in `docs/turso-schema.sql`.

The current integration expects this table:

```sql
site_documents(slug TEXT PRIMARY KEY, json TEXT NOT NULL, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)
```

Expected rows:

- `site-content`
- `update-manifest`

Both rows store JSON strings.

## How to update website content

Update the JSON inside the `site-content` row in Turso, then trigger a new Render deploy.

## How to update app version info

Update both:

- `site-content.version`
- `update-manifest.version`

Then redeploy the site so the new `website/update.json` is generated.