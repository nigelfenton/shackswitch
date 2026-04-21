#!/usr/bin/env bash
# ============================================================
# ShackSwitch v2.0 — Deploy to Arduino Uno Q
# G0JKN — https://github.com/nigelfenton/shackswitch
#
# Usage:
#   ./deploy.sh <board-ip>
#
# Example:
#   ./deploy.sh 10.0.0.56
#
# What it does:
#   1. Copies all Python files and templates to the board
#   2. Ensures app.yaml has ports 5000 and 9007
#   3. Restarts the app (flashes sketch + starts Python)
#
# Requirements:
#   - SSH key at ~/.ssh/id_ed25519_claude
#   - arduino-app-cli on the board (it's built in to Uno Q)
#   - user:first-app must exist on the board (created by App Lab on first run)
# ============================================================

set -e

BOARD_IP="${1}"
# SSH key — auto-detected in order of preference, or override with:
#   SSH_KEY=~/.ssh/mykey ./deploy.sh 10.0.0.56
if [ -z "$SSH_KEY" ]; then
    for candidate in \
        "${HOME}/.ssh/id_ed25519_claude" \
        "${HOME}/.ssh/id_ed25519" \
        "${HOME}/.ssh/id_rsa" \
        "${HOME}/.ssh/id_ecdsa"; do
        if [ -f "$candidate" ]; then
            SSH_KEY="$candidate"
            break
        fi
    done
fi
if [ -z "$SSH_KEY" ]; then
    echo "ERROR: No SSH key found. Set SSH_KEY=/path/to/key and retry."
    exit 1
fi
REMOTE_USER="arduino"
REMOTE_APP_DIR="/home/arduino/ArduinoApps/first-app"
REMOTE_PY="${REMOTE_APP_DIR}/python"
APP_ID="user:first-app"

# ── Validate ────────────────────────────────────────────────
if [ -z "$BOARD_IP" ]; then
    echo "Usage: $0 <board-ip>"
    echo "  e.g. $0 10.0.0.56"
    exit 1
fi

SSH="ssh -i ${SSH_KEY} ${REMOTE_USER}@${BOARD_IP}"
SCP="scp -i ${SSH_KEY}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "=== ShackSwitch Deploy ==="
echo "    Board : ${BOARD_IP}"
echo "    Source: ${SCRIPT_DIR}"
echo ""

# ── Check board is reachable ─────────────────────────────────
echo "[1/4] Checking board reachability..."
if ! $SSH "echo OK" > /dev/null 2>&1; then
    echo "ERROR: Cannot SSH to ${BOARD_IP} — check IP and key"
    exit 1
fi
echo "      Board reachable ✓"

# ── Copy Python files ────────────────────────────────────────
echo "[2/4] Copying Python files..."
PY_FILES=(
    main.py
    nextion.py
    smartsdr.py
    radios.py
    radio_driver.py
    radio_kenwood.py
    radio_yaesu.py
    radio_icom.py
    rfkit.py
    kenwood.py
    requirements.txt
)
for f in "${PY_FILES[@]}"; do
    if [ -f "${SCRIPT_DIR}/${f}" ]; then
        $SCP "${SCRIPT_DIR}/${f}" "${REMOTE_USER}@${BOARD_IP}:${REMOTE_PY}/${f}"
        echo "      ${f} ✓"
    else
        echo "      ${f} — NOT FOUND, skipping"
    fi
done

# Templates
echo "      Copying templates..."
for f in "${SCRIPT_DIR}/templates/"*.html; do
    fname="$(basename "$f")"
    $SCP "$f" "${REMOTE_USER}@${BOARD_IP}:${REMOTE_PY}/templates/${fname}"
    echo "      templates/${fname} ✓"
done

# ── Ensure app.yaml has both ports ───────────────────────────
echo "[3/4] Checking app.yaml port mappings..."
$SSH "cat > ${REMOTE_APP_DIR}/app.yaml << 'EOF'
name: first app
description: \"\"
ports:
- 5000
- 9007
bricks: []
icon: 😀
EOF"
echo "      app.yaml: ports 5000 + 9007 ✓"

# ── Restart app ──────────────────────────────────────────────
echo "[4/4] Restarting app (flashing sketch + starting Python)..."
$SSH "arduino-app-cli app restart ${APP_ID}" 2>&1 | grep -E "✓|ERROR|error" || true
echo ""

# ── Verify ───────────────────────────────────────────────────
sleep 5
echo "Verifying..."
VER=$($SSH "curl -s http://localhost:5000/status 2>/dev/null | python3 -c \"import sys,json; s=json.load(sys.stdin); print(s.get('version','?'))\"" 2>/dev/null || echo "not ready yet")
echo "    App version : ${VER}"
PORTS=$($SSH "docker ps --format '{{.Ports}}' 2>/dev/null | head -1" 2>/dev/null || echo "?")
echo "    Docker ports: ${PORTS}"
echo ""
echo "=== Deploy complete ==="
echo ""
echo "Next steps:"
echo "  1. Open http://${BOARD_IP}:5000 to access the web UI"
echo "  2. Go to Settings → PORTS to name your antennas"
echo "  3. Go to Settings → ANT MAP to assign bands to ports"
echo "  4. In AetherSDR Peripherals tab, set ShackSwitch IP to ${BOARD_IP}"
echo ""
