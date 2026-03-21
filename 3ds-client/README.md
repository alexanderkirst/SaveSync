# 3DS Client (libctru 3DSX MVP)

Nintendo 3DS homebrew sync client using socket-based HTTP with libctru networking.

## Implemented features

1. Config parsing from `sdmc:/3ds/gba-sync/config.ini`
2. Local `.sav` scanning from configurable save directory (`mode=normal` or `mode=vc`)
3. ROM-header-based **`game_id`** when `[rom]` paths are configured (else normalized stem)
4. HTTP sync operations:
   - `GET /saves`
   - `GET /save/{game_id}` (+ `/meta`)
   - `PUT /save/{game_id}` (`force=1` on uploads from this client)
5. **Auto (A):** If the plan has **no** upload/download/skip-no-baseline/conflict work, **skips preview** and prints **Already Up To Date** once (no per-game `OK` lines), then the post-sync menu. Otherwise **preview** lists per-game actions, then **A** runs; **`.gbasync-baseline`** + SHA-256 (legacy **`.savesync-baseline`** supported); first-run **SKIP** until X/Y seeds baseline; **Conflict** UI (X/Y/B) during apply
6. **Save viewer:** main menu **R** lists local + server `game_id`s; **R** toggles lock; **B** back
7. **Upload** / **download** pickers; **START** / **X** (upload) or **START** / **Y** (download) to run batch, **B** back (**R** is not used on these pickers)
8. **Status line** on main menu; **`.gbasync-status`** next to baseline in the active save folder
9. Optional **`sync.locked_ids`** — comma-separated `game_id` list skipped on Auto; **R** toggles lock in **Save viewer** (main menu **R**) and updates **`sdmc:/3ds/gba-sync/config.ini`** (sync **preview** is confirm-only)
10. **Post-sync:** **A** main menu, **START** exit app (skips second exit prompt); **Y: reboot now** when a download ran **or** Auto finished **Already Up To Date** (same as before for download-only)
11. Atomic writes; bottom-screen UI; resilient HTTP (chunked / encoding / JSON tolerance)

## Example config

```ini
[server]
url=http://10.0.0.151:8080
api_key=change-me

[sync]
mode=normal
save_dir=sdmc:/mGBA
vc_save_dir=sdmc:/3ds/Checkpoint/saves
# locked_ids=myhack,other-game

[rom]
rom_dir=sdmc:/roms/gba
rom_extension=.gba
```

Mode notes:

- `mode=normal`: sync from `save_dir`
- `mode=vc`: sync from `vc_save_dir` (useful with exported VC save flows)
