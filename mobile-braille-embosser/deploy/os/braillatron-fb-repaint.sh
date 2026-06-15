#!/usr/bin/env bash
# Repaint HDMI/SPI chrome after network-online (fbcon may clear /dev/fb0 when tty1 init runs late).
set -euo pipefail

pid="$(systemctl show braillatron-ui.service -p MainPID --value 2>/dev/null || echo 0)"
if [[ ! "${pid}" =~ ^[1-9][0-9]*$ ]]; then
  exit 0
fi

kill -USR1 "${pid}" 2>/dev/null || true
