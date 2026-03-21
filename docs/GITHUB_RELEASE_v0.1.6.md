<!-- Paste into GitHub Release: title = GBAsync v0.1.6 -->

## GBAsync v0.1.6

Console-focused release: **Auto sync preview**, **status + locks**, **admin web UI** on the server, and a lot of Switch/3DS UX polish. Server sync API is unchanged for normal clients; **admin routes** are optional when configured.

### Download (attach these from `dist/`)

| Asset | Contents |
|--------|----------|
| **`server-v0.1.6.zip`** | Docker image `gbasync-server-v0.1.6.tar`, `docker-compose.yml`, `.env.example`, `README.txt` |
| **`gbasync-switch-v0.1.6.zip`** | `gbasync.nro`, `.nacp`, `.elf`, `INSTALL.txt`, sample `gba-sync/config.ini` |
| **`gbasync-3ds-v0.1.6.zip`** | `gbasync.3dsx`, `gbasync.cia`, `.smdh`, `INSTALL.txt`, sample `gba-sync/config.ini` |

Archive paths inside the zips match the **`dist/...`** layout used in **v0.1.5** (e.g. `dist/switch/gbasync-switch-v0.1.6/...`).

**Bridge** (desktop / Delta) is **not** a default GitHub attachment: build from source with `./scripts/release-bridge.sh` if you need `dist/bridge/gbasync-bridge-v*.zip`.

**Admin UI:** bundled **inside** the Docker image (`admin-web`); no extra zip. Set **`GBASYNC_ADMIN_PASSWORD`** in `.env` and open **`/admin/ui/`**.

---

### Highlights

**Auto sync (Switch + 3DS)**  
- **Plan → preview → apply**; preview shows **non-OK** rows only; **A** confirm / **B** cancel.  
- **Already Up To Date** when **all** plan rows are OK (fixes “all locked” false positive).  
- Locks / **`locked_ids`** edited in **Save viewer** (main menu **R**), not on the preview screen.

**Status & locks**  
- **`.gbasync-status`** next to baseline: last sync, server, Dropbox, optional error.  
- **`[sync] locked_ids=`** in `config.ini`.

**Server**  
- **Admin web UI** at **`/admin/ui/`** (dashboard, saves, conflicts, index routing, slot map, actions) when **`GBASYNC_ADMIN_PASSWORD`** is set.

**Switch**  
- **Auto sync** menu label; two-column hints; status on **three lines**; **one `+`** to exit from main menu.  
- **Scanning local saves…** before Auto scan; **Dropbox sync now…** before Dropbox request.  
- Auto **post-sync log** omits **`Local saves:`** / **`Remote saves:`**; blank line then per-game results (like 3DS).

**Both consoles**  
- Less flicker (**dirty** redraws); upload/download pickers show **`game_id` only**.  
- **512-byte** ROM header read for `game_id` when **`[rom]`** is set (faster than reading full ROMs per save).

---

### Upgrade

- Replace **`gbasync.nro`** / **`gbasync.3dsx`** (or `.cia`); keep your **`config.ini`**.  
- Server: load new image from **`server-v0.1.6.zip`**, merge **`.env`** as needed.  
- Optional: enable admin UI via **`GBASYNC_ADMIN_PASSWORD`** — see **`admin-web/README.md`**.

### Limits

- Consoles: **HTTP** only (no TLS in this build path).  
- Sync remains **foreground** / manual on devices.

---

*Full notes: `docs/RELEASE_NOTES_v0.1.6.md`*
