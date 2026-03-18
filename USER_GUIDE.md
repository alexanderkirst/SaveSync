# SaveSync User Guide

This guide explains how to run SaveSync end-to-end across:

- self-hosted server
- desktop Delta bridge
- Nintendo Switch homebrew client
- Nintendo 3DS homebrew client

## 1) Start the server

### Option A: Docker (recommended)

```bash
cd server
cp .env.example .env
docker compose up -d
```

Verify:

```bash
curl http://127.0.0.1:8080/health
```

Expected response:

```json
{"status":"ok"}
```

### Option B: Python directly

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
uvicorn app.main:app --host 0.0.0.0 --port 8080
```

Expected startup log snippet:

```text
INFO:     Uvicorn running on http://0.0.0.0:8080
```

## 2) Configure and run Delta bridge

1. Copy `bridge/config.example.json` to `bridge/config.json`
2. Set:
   - `server_url`
   - `api_key`
   - `delta_save_dir`

Run once:

```bash
cd bridge
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python bridge.py --config config.json --once
```

Expected one-shot output example:

```text
[upload] Pokemon Emerald.sav -> pokemon-emerald
```

Continuous watch mode:

```bash
python bridge.py --config config.json --watch
```

Expected watch-mode output example:

```text
[watch] started
[download] pokemon-emerald -> Pokemon Emerald.sav
```

## 3) Install Switch client

Use artifact:

- `dist/switch/savesync-switch-v0.1.0/gbasync.nro`

On SD card:

- `sdmc:/switch/gbasync.nro`
- `sdmc:/switch/gba-sync/config.ini`

Config:

```ini
[server]
url=http://YOUR_SERVER_IP:8080
api_key=change-me

[sync]
save_dir=sdmc:/roms/gba/saves
```

Launch from Homebrew Menu and press `+` to exit.

Expected in-app output example:

```text
Local saves: 3
Remote saves: 2
pokemon-emerald: UPLOADED
metroid-zero: OK
```

## 4) Install 3DS client

Use artifact:

- `dist/3ds/savesync-3ds-v0.1.0/gbasync.3dsx`

On SD card:

- `sdmc:/3ds/gbasync.3dsx`
- `sdmc:/3ds/gba-sync/config.ini`

Config:

```ini
[server]
url=http://YOUR_SERVER_IP:8080
api_key=change-me

[sync]
save_dir=sdmc:/saves
```

Launch from Homebrew Launcher and press `START` to exit.

Expected in-app output example:

```text
Scanning local saves...
Local saves: 2
Remote saves: 3
pokemon-emerald: DOWNLOADED
```

## 5) Validate cross-device sync

1. Run one client (Delta/Switch/3DS) and make save progress.
2. Sync that client.
3. Run another client and sync.
4. Confirm same progress appears on second client.
5. Verify metadata via:

```bash
curl -H "X-API-Key: change-me" http://YOUR_SERVER_IP:8080/saves
```

## 6) Common issues

- **401 unauthorized**: API key mismatch.
- **No saves found**: wrong `save_dir`, missing `.sav` files.
- **Cannot connect**: wrong server IP/port or firewall block.
- **No cross-device update**: sync one side, then sync the other side.
- **Conflicts**: check `GET /conflicts`, resolve via `POST /resolve/{game_id}`.

## 7) Current MVP limits

- Switch/3DS clients currently use HTTP.
- Game ID is filename-derived in this version.
- Console sync is manual (app-triggered), not background.

## 8) Dist artifact reference

### `dist/server`

- `savesync-server-vX.Y.Z.tar`: Docker image archive for the server.
- `docker-compose.yml`: Compose file to run server container.
- `README.txt`: quick load/run instructions for that packaged image.

### `dist/bridge`

- `savesync-bridge-vX.Y.Z.zip`: distributable desktop bridge package.
- `bridge.py`: bridge app entry point.
- `config.example.json`: bridge config template.
- `requirements.txt`: Python deps for bridge runtime.
- `run-once.sh`: helper script for one-shot sync.
- `run-watch.sh`: helper script for continuous watch mode.
- `README.md`: bridge-specific notes.

### `dist/switch`

- `gbasync.nro`: runnable Switch homebrew app (what users launch).
- `gbasync.nacp`: app metadata (title/author/version), consumed by tooling.
- `gbasync.elf`: raw executable/debug build artifact (not typically copied to SD for normal use).
- `INSTALL.txt`: end-user install instructions.

### `dist/3ds`

- `gbasync.3dsx`: runnable 3DS homebrew app (what users launch).
- `gbasync.smdh`: icon/metadata asset used by Homebrew Launcher.
- `INSTALL.txt`: end-user install instructions.
