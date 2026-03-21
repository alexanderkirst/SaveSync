# GBAsync v0.1.6 — Console UX, admin UI, and polish

Release focused on **homebrew console clients** (Switch + 3DS), a first **admin web UI** on the server, and many small UX/performance fixes. Server API behavior is largely unchanged except for **optional admin routes** when configured.

---

## Highlights

### Auto sync (both consoles)

- **Plan → preview → apply:** Auto runs a **read-only** plan (local scan, `GET /saves`, baseline), shows a **preview** of non-OK work (upload / download / skip / conflict / lock), then applies after **A** confirm (**B** cancels).
- **Already Up To Date** only when **every plan row is OK** (`nk == plan size`), not when “zero work counts” (which also matched **all locked** games).
- Preview lists **non-OK rows only**; header counts **omit OK**; **no lock toggle on preview** — use **Save viewer** for `locked_ids`.
- **Apply** omits noisy **`game_id: OK`** lines when hashes already match.

### Status, locks, save viewer

- **`.gbasync-status`** next to **`.gbasync-baseline`**: last sync, server reachability, Dropbox last result, optional short error.
- **`[sync] locked_ids=`** in `config.ini`; locked games are **skipped** on Auto with a log line.
- **Save viewer** (main menu **R**): union of local + server `game_id`s; **R** toggles lock and persists INI.
- **3DS:** large INI rewrites for locks use **heap** buffers (avoid stack overflow on default thread stack).

### Admin web UI (server)

- Static UI under **`/admin/ui/`** when **`GBASYNC_ADMIN_PASSWORD`** is set: Dashboard, Saves, Conflicts, index routing (aliases / ROM SHA1 / tombstones), optional **Slot map**, Actions (Dropbox sync-once, resolve conflict, delete save with confirmation).
- Auth: **HttpOnly** cookie or **`X-API-Key`**. See **`admin-web/README.md`**.

### Switch-specific

- Main menu: **Auto sync** label; **two-column** key hints; **Last sync** / **Server** / **Dropbox** on **separate lines** (like 3DS); blank line after **Save dir** before status.
- **Single `+`** exits from the main menu (no second “Press +” screen); config errors still show **Press +** once.
- **Scanning local saves…** before Auto scan (live `consoleUpdate`).
- **Dropbox sync now…** before the sync-once HTTP call.
- **Auto post-sync log:** no **`Local saves:`** / **`Remote saves:`** lines; **blank line** after confirm, then per-game **`UPLOADED` / `DOWNLOADED` / …** (aligned with 3DS flow).

### 3DS-specific

- **AUTO** flow matches Switch **plan → preview → apply**; preview/error screens use **dirty** redraw and **`consoleClear`** where needed.

### Both consoles — UI polish

- **Dirty** redraw: save viewer, Switch upload/download pickers; static error/empty screens **draw once**.
- **Save viewer** controls: **one action per line** (move, lock toggle, back).
- **Upload/download pickers:** rows show **`game_id` only** (no filename in parentheses).
- **ROM `game_id` resolution:** when **`[rom]`** is set, only the **first 512 bytes** of each matching ROM are read for the GBA header (major speedup vs reading full ROMs per save).

### Docs

- Root **`README.md`** and **`docs/USER_GUIDE.md`** updated for Auto sync, status, locks, admin UI, and ROM header behavior.

---

## Artifacts (typical layout)

Built with:

```bash
./scripts/release-server.sh v0.1.6
./scripts/release-bridge.sh v0.1.6
./scripts/release-switch.sh v0.1.6
./scripts/release-3ds.sh v0.1.6
```

- **`dist/server/gbasync-server-v0.1.6.tar`** — Docker image load (`docker load -i …`). The image **includes** the **`admin-web/`** static files (see `server/Dockerfile` → `COPY admin-web ./admin-web`); there is **no separate** admin UI zip — enable it with **`GBASYNC_ADMIN_PASSWORD`** in `.env` after deploy. If you run the API **without** Docker from a git checkout, keep the repo’s **`admin-web/`** next to the app so the same mount path works.
- **`dist/bridge/gbasync-bridge-v0.1.6.zip`** — desktop bridge package
- **`dist/switch/gbasync-switch-v0.1.6/`** — `gbasync.nro`, `gbasync.nacp`, `INSTALL.txt`, sample `gba-sync/config.ini`
- **`dist/3ds/gbasync-3ds-v0.1.6/`** — `gbasync.3dsx`, `gbasync.cia`, `INSTALL.txt`, sample `gba-sync/config.ini`

Switch **`.nacp`** version string comes from **`switch-client/Makefile`** `APP_VERSION` (set to **0.1.6** for this release).

---

## Upgrade notes

- Copy new **`gbasync.nro`** / **`gbasync.3dsx`** (or `.cia`) over prior builds; keep your existing **`config.ini`**.
- Optional: set **`GBASYNC_ADMIN_PASSWORD`** (and related vars) in server `.env` to enable **`/admin/ui/`** — see **`admin-web/README.md`**.
- No mandatory server API migration for basic sync; admin routes are additive when enabled.

---

## Known limitations (unchanged)

- Console clients use **`http://`** only (no TLS in this build path).
- Sync is **foreground** / manual on consoles.
