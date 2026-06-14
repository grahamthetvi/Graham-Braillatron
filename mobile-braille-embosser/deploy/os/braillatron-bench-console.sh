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

liblouis_ready=0
if pkg-config --exists liblouis 2>/dev/null &&
   [[ -f /usr/share/liblouis/tables/en-ueb-g2.ctb ]]; then
  liblouis_ready=1
fi

cd "${DAEMON_DIR}"
if [[ "${liblouis_ready}" -eq 1 ]]; then
  echo "braillatron-bench: building with liblouis translation" >&2
  make BRAILLATRON_LIBLOUIS=1 display
  ./braillatron-liblouis-test
else
  echo "braillatron-bench: liblouis not found; building stub (chords navigate but do not translate)" >&2
  echo "braillatron-bench: install liblouis-devel and liblouis-data for braille letters" >&2
  make display
fi

export BRAILLATRON_CONFIG=config
exec ./braillatron-ui
