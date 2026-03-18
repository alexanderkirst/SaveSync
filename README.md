# SaveSync

Cross-platform GBA save synchronization across:

- Nintendo Switch (homebrew)
- Nintendo 3DS (homebrew)
- Delta on iOS (through a desktop bridge)

SaveSync lets you continue the same GBA game on different devices by syncing `.sav` files through a self-hosted server.

## Why This Project

Delta has cloud sync, but it is Delta-specific. SaveSync provides one shared sync system across multiple emulators/hardware targets with a consistent server-side source of truth.

## Architecture

SaveSync uses a client-server model:

- `server/`: FastAPI backend that stores save binaries + metadata
- `bridge/`: desktop sync bridge for Delta save folders
- `switch-client/`: Switch homebrew sync app (`.nro`)
- `3ds-client/`: 3DS homebrew sync app (`.3dsx`)

Each client compares local and remote timestamps and performs upload/download as needed.

## Current Status

- [x] Server MVP
- [x] Delta bridge MVP
- [x] Switch client MVP
- [x] 3DS client MVP
- [x] Release/packaging scripts
- [x] End-user installation guides

## Quick Start

### 1) Start server

```bash
cd server
cp .env.example .env
docker compose up -d
```

Health check:

```bash
curl http://127.0.0.1:8080/health
```

### 2) Run Delta bridge once

```bash
cd bridge
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp config.example.json config.json
python bridge.py --config config.json --once
```

### 3) Install console clients

Generate release artifacts:

```bash
./scripts/release-server.sh v0.1.0
./scripts/release-bridge.sh v0.1.0
./scripts/release-switch.sh v0.1.0
./scripts/release-3ds.sh v0.1.0
```

Then use:

- `dist/switch/.../INSTALL.txt`
- `dist/3ds/.../INSTALL.txt`

## Documentation

- `USER_GUIDE.md`: full setup and usage guide
- `RELEASE.md`: packaging and release workflow
- `dist/README.md`: dist artifact glossary and install pointers
- `server/README.md`: server API and run details
- `PLAN.md`: implementation checklist and progress tracking
- `IDEA.md`: project concept notes

## Key Behaviors

- Last-write-wins based on `last_modified_utc`
- Conflict detection when hashes differ at same timestamp
- Atomic file writes for safer save replacement
- Optional version-history backups on server

## MVP Limitations

- Switch/3DS clients currently use `http://` (no TLS path yet)
- Game identity is filename-derived in current MVP
- Console sync is foreground/manual (not background service)

## Toolchain Requirements (for building clients)

- Python 3.11+ (server/bridge)
- Docker Desktop (server container workflow)
- devkitPro with:
  - `devkitA64` + `libnx` (Switch)
  - `devkitARM` + `libctru` (3DS)

## Contributing / Next Work

Planned improvements:

- ROM-header-based `game_id` normalization
- HTTPS support for console clients
- richer conflict resolution UX
- telemetry/log rotation and deeper integration tests
