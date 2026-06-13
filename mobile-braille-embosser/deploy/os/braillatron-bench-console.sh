#!/usr/bin/env bash
# Build (display + ncurses) and run braillatron-ui for local bench development.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DAEMON_DIR="${ROOT}/daemon-dietpi"
CONFIG_DIR="${DAEMON_DIR}/config"

if [[ ! -d "${DAEMON_DIR}" ]]; then
  echo "braillatron-bench: daemon directory not found: ${DAEMON_DIR}" >&2
  exit 1
fi

if ! grep -q '^evdev_enabled=true' "${CONFIG_DIR}/keyboard.conf" 2>/dev/null; then
  echo "braillatron-bench: enabling USB keyboard bench mode" >&2
  cp "${CONFIG_DIR}/keyboard-bench.conf" "${CONFIG_DIR}/keyboard.conf"
fi

cd "${DAEMON_DIR}"
make display

export BRAILLATRON_CONFIG=config
exec ./braillatron-ui
