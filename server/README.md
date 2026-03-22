# GBAsync Server

FastAPI backend for binary save storage and metadata coordination.

## Features (MVP)

- API-key protected endpoints
- Binary save upload/download
- Metadata index with timestamp + hash
- Supports forced overwrite uploads (`force=1`)
- Conflict flag on equal timestamp + different hash
- Atomic write safety
- Optional version-history backup

## Endpoints

- `GET /health`
- `GET /saves`
- `GET /conflicts`
- `GET /save/{game_id}/meta`
- `GET /save/{game_id}`
- `PUT /save/{game_id}`
- `DELETE /save/{game_id}` — remove index row and delete the stored blob (cleanup / bad test data)
- `POST /resolve/{game_id}`

## Save history (when `ENABLE_VERSION_HISTORY=true`)

- `GET /save/{game_id}/history` — list backup revisions (includes **`keep`** pin state and optional **`display_name`** per revision when labels exist).
- `POST /save/{game_id}/restore` — restore the active save from a named history file (server-side only).
- `PATCH /save/{game_id}/history/revision/keep` — pin or unpin a history file (`filename`, `keep`); pinned files are stored in **`pins.json`** under that game’s history directory and are **not** trimmed first when enforcing **`HISTORY_MAX_VERSIONS_PER_GAME`**.
- `PATCH /save/{game_id}/meta` — optional **`display_name`** for the main index row (friendly label).

See **`docs/USER_GUIDE.md`** for retention behavior and console flows.

## Admin web UI (optional)

When `GBASYNC_ADMIN_PASSWORD` is set, open **`http://<host>:8080/admin`** (redirects to `/admin/ui/`). Log in with that password, or call `/admin/api/*` with **`X-API-Key`** (same as the main API) instead of a browser session. If the password env var is unset, admin routes return **404** (disabled).

## Data directory (`save_data/`)

`GET /saves` is driven by the **metadata index** (`INDEX_PATH`, e.g. `save_data/index.json`), not by re-scanning the save folder. Curl uploads register rows there; deleting local test files does **not** remove server metadata. Use `DELETE` or edit the index JSON if entries outlive the files.

By default this repo stores server data under **`save_data/` at the repository root**:

| Role | Env var | Default in `.env.example` (uvicorn cwd = `server/`) |
|------|---------|-----------------------------------------------------|
| Per-game `.sav` files | `SAVE_ROOT` | `../save_data/saves` |
| Metadata index JSON | `INDEX_PATH` | `../save_data/index.json` |
| Version history (optional) | `HISTORY_ROOT` | `../save_data/history` |

**Where this is configured:** the **repo-root `.env`** (see `.env.example`). `server/app/main.py` reads those variables; it does not hardcode `save_data` — that name is only the conventional folder used in the template paths.

**Local uvicorn:** run from the `server/` directory so `../save_data/...` resolves to the repo’s `save_data/` folder.

**Docker:** `server/docker-compose.yml` mounts `../save_data` to `/data` in the container and sets `SAVE_ROOT`, `INDEX_PATH`, and `HISTORY_ROOT` to `/data/saves`, `/data/index.json`, and `/data/history`. To use a different host location, change the **left-hand side** of the volume mapping (e.g. bind mount a NAS path to `/data`) and keep the in-container paths and `environment:` block in sync — or point the three env vars at another layout you mount explicitly.

**Changing location:** edit all three paths together (and migrate `saves/`, `index.json`, and `history/` if moving an existing install). Stop the server first and copy the data tree so blobs and index stay consistent.

## Run

**Docker (from `server/`, repo-root `.env`):** see root `USER_GUIDE.md`. The image entrypoint runs **uvicorn** and optionally a Dropbox **sidecar** when `SAVESYNC_DROPBOX_MODE` is not `off`.

**Local uvicorn:**

```bash
cp ../.env.example ../.env   # once, at repository root
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --reload --host 0.0.0.0 --port 8080
```

## Upload request example

```bash
curl -X PUT "http://127.0.0.1:8080/save/pokemon-emerald?last_modified_utc=2026-03-17T21:00:00%2B00:00&sha256=<sha>&size_bytes=131072&filename_hint=Pokemon%20Emerald.sav&platform_source=delta-bridge" \
  -H "X-API-Key: change-me" \
  --data-binary @Pokemon\ Emerald.sav
```

Delete a save from the index (and remove `pokemon-emerald.sav` under `SAVE_ROOT`):

```bash
curl -X DELETE "http://127.0.0.1:8080/save/pokemon-emerald" -H "X-API-Key: change-me"
```

Force-overwrite example:

```bash
curl -X PUT "http://127.0.0.1:8080/save/pokemon-emerald?last_modified_utc=1970-01-01T00:00:00%2B00:00&sha256=<sha>&size_bytes=131072&filename_hint=Pokemon%20Emerald.sav&platform_source=3ds-homebrew&force=1" \
  -H "X-API-Key: change-me" \
  --data-binary @Pokemon\ Emerald.sav
```
