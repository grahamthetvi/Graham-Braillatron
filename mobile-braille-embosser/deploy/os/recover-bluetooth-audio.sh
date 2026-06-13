#!/usr/bin/env bash
# Recover from a bad audio experiment (PipeWire freeze, wrong routing, etc.).
# Restores aux jack as default. Keeps Bluetooth pairing config if present.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "recover-bluetooth-audio.sh must run as root (sudo)." >&2
  exit 1
fi

bash "${ROOT}/os/setup-aux-audio.sh"

echo ""
echo "Recovery complete. Reboot if the system was frozen: reboot"
