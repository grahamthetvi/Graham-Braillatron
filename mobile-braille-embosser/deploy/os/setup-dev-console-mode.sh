#!/usr/bin/env bash
# Enable HDMI ncurses fallback on Pi (disable BRAILLATRON_HEADLESS override).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-dev-console-mode.sh must run as root (sudo)." >&2
  exit 1
fi

if findmnt -n -o OPTIONS / | grep -q ',ro,'; then
  echo "Root is read-only; remounting read-write..."
  cd /
  mount -o remount,rw /
fi

install -d /etc/braillatron
cat >/etc/braillatron/appliance.env <<'EOF'
# Managed by setup-dev-console-mode.sh / setup-appliance-mode.sh
BRAILLATRON_HEADLESS=0
EOF

echo "HDMI ncurses fallback enabled (BRAILLATRON_HEADLESS=0)."
echo "Re-applying appliance console lockdown..."
BRAILLATRON_HEADLESS=0 bash "${SCRIPT_DIR}/setup-appliance-mode.sh"

echo ""
echo "Dev console mode active. Reboot to show ncurses on HDMI when SPI panel is absent:"
echo "  reboot"
