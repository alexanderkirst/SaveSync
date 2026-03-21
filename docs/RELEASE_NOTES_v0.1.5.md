# GBAsync v0.1.5 — First Public Release

First public release of GBAsync: sync GBA saves between 3DS, Switch, and a self-hosted server, with Dropbox/Delta integration support.

### What’s New

- Full 3DS and Switch save sync clients (upload, download, and automatic newer-wins sync flow)
- FastAPI server for save blob storage + metadata index + conflict handling
- Dropbox integration via Delta Harmony format support
- Server-triggered Dropbox sync pass endpoint and background sidecar behavior
- Improved game mapping logic for hacks/retail collisions
- Persistent Delta slot-map learning and safer skip behavior when mapping is ambiguous
- Release/build scripts and packaged distribution artifacts

### Release Assets

- `server-v0.1.5.zip`
- `gbasync-3ds-v0.1.5.zip`
- `gbasync-switch-v0.1.5.zip`

---

### Quick Install

#### Server (`server-v0.1.5.zip`)

1. Unzip the archive.
2. Load Docker image:
   - `docker load -i gbasync-server-v0.1.5.tar`
3. Create runtime env:
   - `cp .env.example .env`
4. Edit `.env`:
   - Set `API_KEY`
   - Set Dropbox variables if using Dropbox sync
5. Start:
   - `docker compose up -d`
6. Verify:
   - `curl http://<server-ip>:8080/health` -> `{"status":"ok"}`

#### 3DS (`gbasync-3ds-v0.1.5.zip`)

1. Unzip.
2. Copy:
   - `gbasync.3dsx` -> `sdmc:/3ds/gbasync.3dsx`
   - Optional: install `gbasync.cia` with FBI
   - `gba-sync/config.ini` -> `sdmc:/3ds/gba-sync/config.ini`
3. Edit `config.ini`:
   - `url=http://<server-ip>:8080`
   - `api_key=<your-api-key>`

#### Switch (`gbasync-switch-v0.1.5.zip`)

1. Unzip.
2. Copy:
   - `gbasync.nro` -> `sdmc:/switch/gbasync.nro`
   - `gba-sync/config.ini` -> `sdmc:/switch/gba-sync/config.ini`
3. Edit `config.ini`:
   - `url=http://<server-ip>:8080`
   - `api_key=<your-api-key>`

---

### Notes

- This is the first public release and may still have edge cases with unusual ROM/save naming setups.
- If Delta mapping is ambiguous, GBAsync will skip unsafe mappings and log `[skip-map]` rather than risk writing to the wrong slot.
- Manual slot overrides are supported via `delta_slot_map_path` (documented in `bridge/README.md`).

---

### Known Limitations

- Server is currently HTTP by default (recommended for trusted LAN/VPN use).
- Switch metadata version string in `.nacp` may lag tag version in some builds (cosmetic; functionality unaffected).
