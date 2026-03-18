# Delta Bridge

Desktop sync agent that bridges Delta save files and the SaveSync server.

## Modes

- `--once`: one pull/push pass then exit
- `--watch`: filesystem watch + periodic polling
- `--dry-run`: print actions without writing/uploading

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp config.example.json config.json
python bridge.py --config config.json --once
```

## Notes

- Game IDs are derived from normalized `.sav` filename stems in MVP.
- Future enhancement should use ROM-header-derived identifiers when available.
