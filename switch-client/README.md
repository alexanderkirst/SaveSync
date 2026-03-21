# Switch Client (libnx NRO MVP)

Nintendo Switch homebrew sync client using libnx and socket-based HTTP.

## Implemented features

1. Config parsing from `sdmc:/switch/gba-sync/config.ini`
2. Local `.sav` scan from `sdmc:/roms/gba/saves` (configurable)
3. Optional **ROM-header** `game_id` when `[rom]` paths are set (else normalized stem)
4. HTTP sync operations:
   - `GET /saves`
   - `GET /save/{game_id}` (+ `/meta`)
   - `PUT /save/{game_id}` (`force=1` on uploads from this client)
5. **Auto (A):** If the plan has **no** upload/download/skip-no-baseline/conflict work (only OK and/or locked rows), the app **skips the preview** and prints **Already Up To Date** once (no per-game `OK` lines), then the post-sync menu. Otherwise **preview** lists per-game actions, then **A** runs / **B** cancels; baseline **`.gbasync-baseline`** + SHA-256 (legacy **`.savesync-baseline`** supported); first-run **SKIP** until X/Y seeds baseline; **Conflict** UI (X/Y/B) during apply
6. **Save viewer:** main menu **R** lists local + server `game_id`s; **R** toggles lock; **B** back (sync **preview** is confirm-only — no lock toggle on preview)
7. **Upload (X)** / **download (Y)** pickers with checklist; **+** runs, **B** back
8. **Status line** on main menu (last sync / server / Dropbox) persisted in **`.gbasync-status`** next to the baseline file
9. Optional **`sync.locked_ids`** — comma-separated `game_id` list skipped on Auto; **R** toggles lock in **Save viewer** (main menu **R**) and **writes `config.ini`** (same path as at launch: `sdmc:/switch/gba-sync/config.ini`)
10. **Post-sync:** **A** main menu, **+** exit app; after **Auto** when the result was **Already Up To Date**, also **Y: reboot now** (uses `spsm`)
11. Atomic write for downloaded saves; resilient HTTP parsing (chunked bodies, `Accept-Encoding: identity`, etc.)

## Example config

```ini
[server]
url=http://10.0.0.151:8080
api_key=change-me

[sync]
save_dir=sdmc:/mGBA
# locked_ids=myhack,other-game

[rom]
rom_dir=sdmc:/roms/gba
rom_extension=.gba
```
