#!/bin/bash
set -e

BOARD="arduino@10.0.0.145"
KEY="$HOME/.ssh/id_ed25519_claude"
DEST="/home/arduino/ArduinoApps/first-app/python"
SRC="$(cd "$(dirname "$0")" && pwd)/shackswitch-v2"

echo "=== ShackSwitch Deploy ==="
echo "Source : $SRC"
echo "Target : $BOARD:$DEST"
echo ""

echo "Copying Python files..."
scp -i "$KEY" "$SRC"/*.py "$BOARD:$DEST/"

echo "Copying templates..."
ssh -i "$KEY" "$BOARD" "mkdir -p $DEST/templates"
for f in "$SRC"/templates/*.html; do
    scp -i "$KEY" "$f" "$BOARD:$DEST/templates/"
done

echo "Restarting app..."
ssh -i "$KEY" "$BOARD" "arduino-app-cli app restart user:first-app"

echo ""
echo "=== Deploy complete ==="
