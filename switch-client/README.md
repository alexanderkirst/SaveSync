# Switch Client (libnx NRO MVP)

Nintendo Switch homebrew sync client using libnx and socket-based HTTP.

## Implemented features

1. Config parsing from `sdmc:/switch/gba-sync/config.ini`
2. Local `.sav` scan from `sdmc:/roms/gba/saves` (configurable)
3. HTTP sync operations:
   - `GET /saves`
   - `GET /save/{game_id}`
   - `PUT /save/{game_id}`
4. Timestamp-driven push/pull logic
5. Atomic write for downloaded saves
6. Console UI sync status output

## Example config

```ini
[server]
url=http://192.168.1.50:8080
api_key=change-me

[sync]
save_dir=sdmc:/roms/gba/saves
```
