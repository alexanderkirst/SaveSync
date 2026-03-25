# Ideas & future features (user lens)

Casual backlog: things that could make GBAsync nicer **from a player’s point of view**—not a commitment, not in priority order. Pair with **`docs/TODO.md`** for maintainer-facing work.

---

## “Just make it easier”

- **First-run wizard on consoles** — Prompt for server URL + API key once, test connection, show a green check before dropping into the menu.
- **QR code pairing** — Show a QR on the admin UI (or a tiny printed page) encoding `http://192.168.x.x:8080` + optional API key fragment so phone or a helper app can paste config without typos.
- **Plain-language errors** — Map `401`, timeouts, and DNS failures to “Wrong password,” “Server not reachable,” “Check Wi‑Fi,” with a **copy diagnostics** line for Discord/help forums.
- **One-tap “sync status” export** — Redacted log + app version + server URL (masked) saved to SD for support threads.

---

## Trust & safety

- **Optional HTTPS** — Reverse-proxy story is already the real answer; a short **“Caddy / nginx + Let’s Encrypt”** recipe in the user guide lowers the bar for non-LAN use.
- **Separate API keys per device** — Family server: revoke one handheld without rotating everything (needs server + auth model work).
- **Read-only API key** — Download-only clients for a kid’s device or a backup machine.
- **Admin session hardening** — Optional 2FA, IP allowlist, or time-limited admin tokens for people who expose **`/admin`** wider than the LAN.

---

## When sync hurts

- **Resume / chunked uploads** — Huge saves or flaky Wi‑Fi: retry without starting from zero (mostly relevant for NDS or future bigger payloads).
- **Bandwidth cap** — Optional server-side throttle so Dropbox bridge doesn’t saturate a slow uplink during Harmony sync.
- **“Quiet hours”** — Skip upload-triggered Dropbox runs at night (env + cron story).
- **Clearer ETA** — Progress for large downloads (“~2 min left”) on Switch/3DS when feasible.

---

## Organization & discovery

- **Tags or collections** — e.g. “JRPGs,” “multiplayer,” filter in admin and in console save viewers.
- **Cover art / icons** — Optional box art URL per `game_id` in the index; makes the admin and viewers feel less like a spreadsheet.
- **Notes per game** — “Physical cart,” “ROM hack,” “Delta only”—stored on server, shown in UI.
- **Stronger search** — Fuzzy search on display name + `game_id` in admin when the library is huge.

---

## Platforms beyond today

- **Android / plain iOS file sync** — Not Delta-specific: a minimal “folder watcher + HTTP” app or documented **FolderSync** / **Shortcuts** flows pointing at **`bridge.py`** or a tiny mobile helper.
- **Steam Deck / Linux handheld** — Same as desktop bridge: documented **systemd user unit** running **`bridge.py --watch`** against a RetroArch save root.
- **Windows tray app** — Optional tiny GUI that wraps **`bridge.py`** with “Sync now” and a green/red icon.

---

## Power users & automation

- **Webhooks** — `POST` to Discord/Matrix when a save updates, conflict appears, or history is trimmed—fun for streamers and co-op households.
- **CLI beside the server** — `gbasync-cli pull pokemon-emerald --output ./backups/` using the same API key.
- **Bulk export** — “Download everything as a dated zip” from admin for cold-storage backup.
- **Scheduled server backups** — Document **restic** / **Time Machine** on **`save_data/`**, or a one-button **export index + blobs** tarball.

---

## Delta & cloud edge cases

- **Conflict chooser playbook** — Short video or animated GIF in docs: “first time Delta disagrees, tap Cloud vs Local in this order.”
- **Per-game “last known good Harmony rev”** in admin — Debugging only, but saves a support round-trip when Dropbox and server fight.

---

## Polish & feel

- **Themes** — Dark/light for admin UI (many people live in the panel during long sessions).
- **Keyboard shortcuts** in admin — `j`/`k` to move selection, `/` to search.
- **Localization** — If contributors appear: strings for FR/DE/JA for admin + console menu text.
- **Achievement-adjacent stats** — Silly but fun: “Total syncs,” “Most conflicted game,” local-only badge in the app (no telemetry required).

---

## Wilder (maybe never)

- **End-to-end encryption** of save blobs at rest — Paranoid LAN or untrusted backup disk; huge design cost.
- **Federation** — Two servers handshake—almost certainly out of scope for a small self-hosted tool.
- **Official Delta partnership** — A wish, not a feature; file-level sync is the realistic path.

---

## How to use this list

- Pick one row, open a **discussion** or **issue** with a user story (“As a player who… I want… so that…”).
- If an idea needs design, link **`docs/USER_GUIDE.md`** and **`docs/TODO.md`** so implementation doesn’t fork the mental model.

*Add new bullets anytime; trim or promote to **`docs/TODO.md`** when you’re ready to build.*
