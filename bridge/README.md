# GBAsync bridge (desktop / CLI tools)

Python tools that sync **local `.sav` files** or **Dropbox** (plain folders or **Delta Emulator / Harmony** trees) with a **GBAsync server**. Shared **`game_id`** logic lives in **`bridge/game_id.py`** and matches the Switch/3DS clients where possible.

---

## Do I need this folder?

| Your setup | Need standalone `bridge/`? |
|------------|------------------------------|
| **Docker server** + Switch/3DS only | **No** — consoles use HTTP to the server. |
| **Docker server** + **`GBASYNC_DROPBOX_MODE`** = `plain` or `delta_api` | **No** for running scripts — the **container** ships **`/app/bridge`**, runs **`write_bridge_config.py`**, and a **sidecar** calls the same Python entrypoints. You only edit **`.env`** (see **`docs/USER_GUIDE.md`**). |
| **No Docker** on a PC/Mac, but you want to sync a **local** folder of `.sav` with the server | **Yes** — use **`bridge.py`** from this directory (or the **`dist/bridge`** zip). |
| **Harmony folder only on disk** (Dropbox desktop app) on a machine **without** the GBAsync container | **Yes** — run **`delta_folder_server_sync.py`** on that host. |
| **Harmony only in Dropbox** (no local Delta tree), and **not** using Docker sidecar | **Yes** — run **`delta_dropbox_api_sync.py`**. |

If anything in the table is unclear, read **`docs/USER_GUIDE.md`** first.

---

## Prerequisites

1. **Python 3.11+** (3.12 is fine).
2. From this directory:

   ```bash
   cd bridge
   python3 -m venv .venv
   source .venv/bin/activate   # Windows: .venv\Scripts\activate
   pip install -r requirements.txt
   ```

3. **Dropbox API scripts** additionally need:

   ```bash
   pip install -r requirements-dropbox.txt
   ```

4. For **`delta_dropbox_api_sync.py`** / **`dropbox_bridge.py`**, put **Dropbox credentials** in the **repository-root** **`.env`** (same file as the server). See **`DROPBOX_SETUP.md`**.

---

## Scripts at a glance

| Script | What it does |
|--------|----------------|
| **`bridge.py`** | Sync a **local** directory of plain **`*.sav`** files ↔ GBAsync server (`--once` / `--watch` / `--dry-run`). |
| **`dropbox_bridge.py`** | Sync a **Dropbox API** path of **flat** `*.sav` files ↔ server (**not** Harmony layout). |
| **`delta_folder_server_sync.py`** | Merge **server** ↔ **Delta Emulator folder on disk** (Harmony JSON + blobs); `sync_mode` `triple` or `server_delta`. |
| **`delta_dropbox_api_sync.py`** | Same merge idea as above, but **download → merge → upload** via **Dropbox HTTP API** (no local Delta tree required). |
| **`delta_dropbox_sav.py`** | **List / export / import** Harmony saves as `.sav` for manual round-trips. |

Details on Harmony file layout: **`DELTA_DROPBOX_FORMAT.md`**. Dropbox env and JSON: **`DROPBOX_SETUP.md`**, **`DROPBOX.md`**.

---

## Local copies, non-Dropbox sync, and Syncthing-style workflows

**Two different ideas:**

1. **Where the server stores its canonical files**  
   Configure **`SAVE_ROOT`** / **`INDEX_PATH`** / **`HISTORY_ROOT`** (see **`server/README.md`**) to point at **any** directory on disk. In Docker, that usually means the **left-hand side** of the volume mount is a folder you care about—e.g. a path that **Syncthing**, **Resilio**, a cloud-drive sync folder, or **NFS** already replicates. GBAsync does **not** integrate with those tools; it just writes files. **No** `bridge.py` is required for “my server data lives on a synced disk.”

2. **A separate mirror of plain `.sav` files**  
   If you want a **second** tree of **only** `*.sav` files (same names/keys as the server expects) that stays aligned with the GBAsync API—so another app can watch **that** folder—use **`bridge.py`**: set **`delta_save_dir`** in **`config.json`** to the folder path. Run **`bridge.py`** on a schedule or with **`--watch`**. That folder can itself live inside a directory synced by **anything** you like; GBAsync only updates the `.sav` files via **`bridge.py`**.

**Dropbox** in this repo means **Dropbox’s API** or the **Harmony** layout—not “the only way to get files off the server.” For Delta on iOS, **Harmony** integration is separate; for “my own disk + my own sync,” use **(1)** and/or **(2)** above.

---

## Setup: `bridge.py` (local folder ↔ server)

1. `cp config.example.json config.json`
2. Edit **`config.json`**: **`server_url`**, **`api_key`**, **`delta_save_dir`** (folder of `.sav` files), optional **`rom_dirs`** / **`rom_map_path`** / **`poll_seconds`**.
3. Run once:

   ```bash
   python3 bridge.py --config config.json --once
   ```

4. Or continuous watch:

   ```bash
   python3 bridge.py --config config.json --watch
   ```

---

## Setup: `dropbox_bridge.py` (flat `.sav` in Dropbox ↔ server)

1. Complete **`DROPBOX_SETUP.md`** (`.env` + Dropbox app).
2. Copy **`config.example.dropbox.json`** → e.g. `config.dropbox.json`; set **`dropbox.remote_folder`**, **`server_url`**, **`api_key`**.
3. `pip install -r requirements-dropbox.txt`
4. Run:

   ```bash
   python3 dropbox_bridge.py --config config.dropbox.json --once
   ```

   Use **`--watch`** for a loop (see script help).

---

## Setup: `delta_folder_server_sync.py` (Harmony folder on disk ↔ server)

1. Install deps: `pip install -r requirements.txt`
2. Copy **`config.example.delta_sync.json`** → `config.delta_sync.json`.
3. Set **`delta_root`** to your **Delta Emulator** folder (as synced by Dropbox desktop), **`server_url`**, **`api_key`**, **`rom_dirs`**, **`sync_mode`** (`triple` or `server_delta`), optional **`delta_slot_map_path`**, **`rom_map_path`**, **`rom_extensions`**.
4. Run:

   ```bash
   python3 delta_folder_server_sync.py --config config.delta_sync.json --once
   ```

Repeat on a schedule (cron, launchd) if needed—only **`--once`** is built in.

### How Delta titles map to server `game_id`

Matching is deterministic and ordered:

1. **Pinned slot-map first** (`delta_slot_map_path`): if a Harmony slot id already maps to a server `game_id`, that wins.
2. **Exact ROM fingerprint**: if server metadata has `rom_sha1` and it matches Delta ROM `sha1Hash`, use that `game_id`.
3. **Title-derived key match** from Delta display name:
   - sanitized slug (example: `pokescape-2.0.0`)
   - collapsed slug with hyphens removed (example: `pokescape2.0.0`)
   - reduced retail alias (drops words like `pokemon`, `version`) so names like `Pokemon Fire Red Version` can match `firered`
4. **Filename-hint match**: compare those same normalized forms against server `filename_hint` stem (example: `FireRed.sav` -> `firered`).
5. **Header-hint fallback**: if a retail title clearly matches a cartridge header id and that id exists on server, use it.
6. **Unique payload-hash fallback** (bootstrap only): if exactly one server save has same payload `sha256`, map to it.

If none of the above are unambiguous, the slot is skipped for safety and a `[skip-map]` line is logged.

On first successful mapping, the bridge writes it to `delta_slot_map_path` so future runs stay stable even if titles collide.

### Manual override when a slot is skipped

If logs show `[skip-map] ... no server save mapped to this Delta slot`, you can force a mapping by pinning the Harmony slot id to a server `game_id`.

1. Find the Harmony slot id:
   - From logs: `[slot-map] learned 'Title': <harmony_id> -> ...`
   - Or inspect Delta files: `GameSave-<harmony_id>-gameSave`
2. Edit `delta_slot_map_path` JSON and add/update:

```json
{
  "<harmony_id>": "<server_game_id>"
}
```

Example:

```json
{
  "41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc": "firered",
  "0f9a...": "redrocket"
}
```

3. Run one sync pass (`--once`) or wait for the next sidecar interval.

Notes:
- Manual slot-map entries have priority and will override automatic title/header matching.
- If `<server_game_id>` does not exist on the server, mapping is ignored and logged as stale.
- Alternative override: create a dedicated server row first (e.g. `PUT /save/redrocket`), then rerun sync so auto-bootstrap can learn and persist the mapping.

---

## Setup: `delta_dropbox_api_sync.py` (Harmony in Dropbox only, no local tree)

Use when the Delta folder exists **only in Dropbox** and you want the [Dropbox HTTP API](https://www.dropbox.com/developers/documentation/http/documentation). Same **repository-root `.env`** as **`DROPBOX_SETUP.md`**; set **`dropbox.remote_delta_folder`** in **`config.example.delta_dropbox_api.json`**. Each pass downloads the Harmony tree, merges, then uploads changed files.

```bash
python3 delta_dropbox_api_sync.py --config config.delta_dropbox_api.json --once
```

Current behavior for safer Delta sync:

- Upload order is **blob files first**, then `GameSave-*` JSON sidecars.
- After uploading a save blob, sidecar `files[0].versionIdentifier` is aligned to the blob's Dropbox `rev`.

---

## `delta_dropbox_sav.py` (inspect / export / import Harmony saves)

If you download the **Delta Emulator** folder from Dropbox, saves live next to JSON metadata as `GameSave-{id}-gameSave` (see **`DELTA_DROPBOX_FORMAT.md`**).

```bash
python3 delta_dropbox_sav.py list --delta-root "/path/to/Delta Emulator"
python3 delta_dropbox_sav.py export --delta-root "..." --out-dir ./savs
python3 delta_dropbox_sav.py import-sav --delta-root "..." --identifier <40-hex> --sav ./updated.sav --backup-dir ./bak
```

Use that to round-trip with GBAsync (export `.sav` → upload via server; download from server → `import-sav` → re-upload to Dropbox). This keeps metadata consistent on **import**.

---

## Environment variable names (Docker vs legacy)

Use **`GBASYNC_*`** names in the repo-root **`.env`** (see **`.env.example`**). Legacy **`SAVESYNC_*`** names are still read by **`server/write_bridge_config.py`** for the same settings.

---

## Is this “Dropbox integration”?

**Partially.** `bridge.py` only reads the filesystem. If Delta (or anything else) stores saves in a folder that **Dropbox’s desktop app** mirrors—e.g. you point `delta_save_dir` at `~/Dropbox/SomeFolder`—then Dropbox is only moving files; GBAsync still sees plain local `.sav` names.

Delta’s **built-in** Dropbox/Google sync uses the **Harmony** stack and **does not** expose a simple “folder of `.sav` files” to third-party tools. You **cannot** point `bridge.py` at Delta’s raw cloud layout on disk in a supported way.

---

## `bridge.py` CLI flags

- **`--once`**: one pull/push pass then exit  
- **`--watch`**: filesystem watch + periodic polling  
- **`--dry-run`**: print actions without writing/uploading  

## Config fields (`bridge.py` JSON)

- **`server_url`**: GBAsync server base URL  
- **`api_key`**: server API key  
- **`delta_save_dir`**: local folder to monitor  
- **`poll_seconds`**: periodic poll interval  
- **`rom_dirs`**: optional list of ROM directories for ROM-header-based `game_id`  
- **`rom_map_path`**: optional JSON mapping save stem → ROM path  
- **`rom_extensions`**: optional ROM extensions used in `rom_dirs` matching  

## Notes

- Stale **`game_id`** rows on the server (e.g. after experiments) are removed with **`DELETE /save/{game_id}`** — see **`server/README.md`** / **`docs/USER_GUIDE.md`** (`index.json` is authoritative for **`GET /saves`**).
- Game ID resolution order in **`bridge.py`**:
  1. If matching ROM is found, derive from GBA ROM header (`title + game code`) (and NDS/GB parsers where applicable in shared code).
  2. Fallback to normalized `.sav` filename stem.
- ROM matching sources:
  - **`rom_map_path`** JSON mapping save stem to ROM path  
  - **`rom_dirs`** + **`rom_extensions`** stem matching  

---

## See also

- **`docs/USER_GUIDE.md`** — end-to-end server + Dropbox + consoles  
- **`server/README.md`** — HTTP API  
- **`DROPBOX_SETUP.md`**, **`DROPBOX.md`**, **`DELTA_DROPBOX_FORMAT.md`**
