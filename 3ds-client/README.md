# 3DS Client (Planned devkitARM/libctru app)

This folder will contain the Nintendo 3DS homebrew sync client.

## Planned implementation steps

1. Set up devkitARM + libctru project (`Makefile`, source layout).
2. Implement config parser for:
   - `sdmc:/3ds/gba-sync/config.ini`
3. Support save directories used by open_agb_firm and custom setups.
4. Implement HTTP sync client with minimal memory footprint.
5. Build sync state engine matching server policy.
6. Add bottom-screen text log with status per save.

## Example config

```ini
[server]
url=http://192.168.1.50:8080
api_key=change-me

[sync]
save_dir=sdmc:/saves
```
