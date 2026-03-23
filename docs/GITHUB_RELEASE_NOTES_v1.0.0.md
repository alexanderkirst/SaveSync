# GBAsync v1.0.0 — first major release

**Pick up the same game on your Switch, your 3DS, your phone, or your PC—without emailing saves or juggling SD cards.**

GBAsync is a **self-hosted** save sync system: **you** run a small server that holds the canonical copy of each game’s `.sav` (and related portable saves). **Homebrew** apps on **Nintendo Switch** and **Nintendo 3DS** talk to it over your network. If you use **Delta** on iOS, optional **Dropbox** integration can keep the same saves aligned with **Harmony**—still under **your** keys and **your** hardware.

No subscription. No cloud vendor deciding how your saves move—just **HTTP**, an **API key**, and clear **preview → confirm** flows so you’re never surprised by a silent overwrite.

---

## Why you might want this

- **One source of truth** — The server stores each game’s save and metadata (hashes, timestamps, optional friendly names). Devices **pull** and **push** when *you* open the app and run a sync.
- **Real hardware + emulators** — Same pipeline for mGBA-style folders on consoles and (optionally) Delta on iPhone: **one system**, not a separate sync story per app.
- **You see changes before they apply** — **Auto sync** builds a **plan**, shows a **preview** of what would happen, then applies when you confirm. **Upload-only** and **download-only** modes exist when you want to cherry-pick games.
- **Conflicts aren’t silent** — If the device and server both moved on from the last known state, you get an explicit **conflict** flow instead of corrupted saves.
- **Optional safety net** — Server-side **version history** (with retention limits and **keep** pins), **restore** from the browser admin or from the console **save viewer**, and **per-game locks** so Auto won’t touch titles you care about.
- **Optional browser admin** — Dashboard, save management, history, uploads from disk, row order, index tools, and Dropbox actions—handy when you don’t want to SSH.

---

## Highlights in v1.0.0

| Area | What you get |
|------|----------------|
| **Server** | FastAPI API, Docker image, atomic writes, index-backed **`GET /saves`**, conflict flags, optional history + labels + pins, Dropbox sidecar when enabled. |
| **Admin UI** | Password-gated **`/admin/ui/`** — saves table, drag-to-reorder + save order, history with restore/keep/labels, upload with optional server-side hashing, slot map when configured. |
| **Switch** | **`.nro`** client — multi-root saves (GBA / NDS / GB·GBC), ROM-header **`game_id`**, Auto preview, conflicts on-device, save viewer with history, status line, lock list in **`config.ini`**. |
| **3DS** | **`.3dsx`** / **`.cia`** — same ideas as Switch; **`normal`** vs **`vc`** modes for open_agb_firm vs Checkpoint-style paths. |
| **Bridge** | Local **`.sav`** folder sync, Dropbox flat folder, Delta folder on disk, Delta-over-Dropbox-API, Harmony-aware merge and mGBA↔Delta size handling where needed. |
| **Docs & packaging** | Release **zips** for server, bridge, Switch, and 3DS with **README** + **`gba-sync/`** install and full **`config.ini`** reference on consoles. |

---

## What’s in the release assets

Typical filenames (tag = **`v1.0.0`**):

- **`gbasync-server-v1.0.0.zip`** — Docker image tarball, Compose, `.env` templates, README (load image → `docker compose up`).
- **`gbasync-bridge-v1.0.0.zip`** — Python bridge scripts, example configs, Dropbox docs, `requirements.txt`.
- **`gbasync-switch-v1.0.0.zip`** — `gbasync.nro`, metadata, **`README.md`**, **`gba-sync/config.ini`** + **`gba-sync/README.md`**.
- **`gbasync-3ds-v1.0.0.zip`** — `gbasync.3dsx`, optional `.cia`, same README / **`gba-sync/`** layout.

Full setup: **`docs/USER_GUIDE.md`**. Maintainer packaging: **`docs/RELEASE.md`**.

---

## Honest limitations (read before you rely on it)

- Console clients use **plain HTTP** on the LAN—**no TLS** on the device path in this release. Treat your **API key** like a password and run on networks you trust.
- Sync is **foreground**: open the app and run it—it’s not a background daemon on Switch/3DS.
- **Delta / Harmony** integration follows Delta’s on-disk/cloud format as faithfully as practical; it is **not** an official Nintendo or Delta product partnership.

---

## License

**GBAsync Non-Commercial License 1.0** — see **`LICENSE`** in the repository.

---

## Links

| | |
|--|--|
| **Repository** | [github.com/drippyday/SaveSync](https://github.com/drippyday/SaveSync) |
| **User guide** | `docs/USER_GUIDE.md` |
| **Detailed release notes** | `docs/RELEASE_NOTES_v1.0.0.md` |

*Thanks for trying GBAsync—happy syncing.*
