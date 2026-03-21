# GBAsync admin web UI

Static browser UI for operating a running **GBAsync server** (saves, conflicts, index routing, optional slot-map view, and a few actions). There is **no build step**: plain HTML, CSS, and JavaScript under `static/`.

## What it does

| Area | Purpose |
|------|--------|
| **Dashboard** | JSON snapshot: Dropbox mode, save/conflict counts, data paths. |
| **Saves** | Lists indexed saves with hash preview, size, conflict flag, last modified; quick link to resolve. |
| **Conflicts** | Saves currently marked in conflict. |
| **Index routing** | Read-only view of index routing data: aliases, `rom_sha1` map, tombstones (from the server index). |
| **Slot map** | If `GBASYNC_SLOT_MAP_PATH` (or legacy `SAVESYNC_SLOT_MAP_PATH`) points at a JSON file the server can read, shows path and parsed JSON; otherwise explains that it is optional. |
| **Actions** | Trigger one Dropbox sync pass (`POST /admin/api/dropbox/sync-once`), resolve a conflict by `game_id`, or delete a save (with confirmation). |

The UI talks only to **`/admin/api/*`** on the same origin (see `static/app.js`). It uses `fetch` with `credentials: "include"` so the session cookie is sent.

## How it is served

The FastAPI app mounts this folder as static files:

- **`/admin/ui/`** → files from `admin-web/static/` (e.g. `index.html`).
- **`/admin`** → HTTP redirect to `/admin/ui/`.
- **`GET /`** in a browser (when `Accept` includes `text/html`) redirects to `/admin/ui/` for convenience.

The Docker image copies `admin-web` into the container so the UI is available next to the Python app. Local runs resolve the repo root and mount the same path.

## Authentication

Admin features are **off** until the server has a non-empty **`GBASYNC_ADMIN_PASSWORD`** in the environment (typically from the repo-root `.env`). If that variable is unset, admin routes respond as **disabled** (e.g. `GET /admin/api/me` reports `admin_enabled: false`, and protected admin APIs return **404**).

When enabled:

1. **Browser login** — You submit the admin password; the server checks it and sets an **HttpOnly** cookie `gbasync_admin_session`. The cookie value is a deterministic **HMAC-SHA256** of the password plus **`GBASYNC_ADMIN_SECRET` if set, otherwise `API_KEY`**. No API key is stored in `localStorage`.
2. **Scripts / curl** — The same admin APIs accept **`X-API-Key`** with the same value as the main GBAsync API key, instead of the cookie.

If neither `API_KEY` nor `GBASYNC_ADMIN_SECRET` is set while a password is set, the admin session cannot be formed (`misconfigured` from `/admin/api/me`).

## Configuration (server)

| Variable | Role |
|----------|------|
| `GBASYNC_ADMIN_PASSWORD` | Enables admin UI and `/admin/api/*` (when non-empty). |
| `API_KEY` | Used for HMAC cookie value (unless `GBASYNC_ADMIN_SECRET` is set) and for `X-API-Key` access to admin APIs. |
| `GBASYNC_ADMIN_SECRET` | Optional; overrides `API_KEY` for the HMAC used in the session cookie only. |
| `GBASYNC_SLOT_MAP_PATH` | Optional path to slot-map JSON for the **Slot map** tab. |

See also `.env.example` in the repository root.

## Developing the UI

Edit files under `static/`. Restart **uvicorn** (or the container) if the server process caches nothing for static files—usually a refresh is enough for asset changes.

There is no package install or bundler for this tree; keep requests same-origin to `/admin/api` so cookies and CORS stay simple.

## Related code

- Server routes: `server/app/admin.py` (prefix `/admin`).
- Static mount and repo layout: `server/app/main.py` (`admin-web/static`).
