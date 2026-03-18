#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRIDGE_DIR="$ROOT_DIR/bridge"
DIST_DIR="$ROOT_DIR/dist/bridge"
VERSION="${1:-dev}"
PKG_DIR="$DIST_DIR/savesync-bridge-${VERSION}"

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"

cp "$BRIDGE_DIR/bridge.py" "$PKG_DIR/"
cp "$BRIDGE_DIR/requirements.txt" "$PKG_DIR/"
cp "$BRIDGE_DIR/config.example.json" "$PKG_DIR/"
cp "$BRIDGE_DIR/README.md" "$PKG_DIR/"

cat > "$PKG_DIR/run-once.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python bridge.py --config config.example.json --once
EOF

cat > "$PKG_DIR/run-watch.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python bridge.py --config config.example.json --watch
EOF

chmod +x "$PKG_DIR/run-once.sh" "$PKG_DIR/run-watch.sh"

(
  cd "$DIST_DIR"
  zip -qr "savesync-bridge-${VERSION}.zip" "savesync-bridge-${VERSION}"
)

echo "Bridge release package created:"
echo "  $DIST_DIR/savesync-bridge-${VERSION}.zip"
