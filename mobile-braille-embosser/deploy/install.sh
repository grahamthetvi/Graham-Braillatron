#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON_DIR="${ROOT}/daemon-dietpi"
PREFIX="${PREFIX:-/usr/local}"
CONFIG_DIR="${CONFIG_DIR:-/etc/braillatron}"
SYSTEMD_DIR="/etc/systemd/system"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install.sh must run as root (sudo)." >&2
  exit 1
fi

echo "Building Braillatron daemons (accessibility backends enabled)..."
make -C "${DAEMON_DIR}" BRAILLATRON_A11Y=1 clean all

install -d "${PREFIX}/bin"
install -m 755 "${DAEMON_DIR}/braillatron-ui" "${PREFIX}/bin/braillatron-ui"
install -m 755 "${DAEMON_DIR}/braillatron-sentinel" "${PREFIX}/bin/braillatron-sentinel"
install -m 755 "${ROOT}/deploy/os/sync-documents.sh" "${PREFIX}/bin/braillatron-sync"

install -d "${CONFIG_DIR}"
install -m 644 "${DAEMON_DIR}/config/hardware.conf" "${CONFIG_DIR}/hardware.conf"
install -m 644 "${DAEMON_DIR}/config/keyboard.conf" "${CONFIG_DIR}/keyboard.conf"
install -m 644 "${DAEMON_DIR}/config/telemetry.conf" "${CONFIG_DIR}/telemetry.conf"
install -m 644 "${DAEMON_DIR}/config/ui.conf" "${CONFIG_DIR}/ui.conf"
install -m 644 "${DAEMON_DIR}/config/matrix_map.conf" "${CONFIG_DIR}/matrix_map.conf"
install -m 644 "${DAEMON_DIR}/config/evdev_map.conf" "${CONFIG_DIR}/evdev_map.conf"
install -m 644 "${DAEMON_DIR}/config/kinematics.conf" "${CONFIG_DIR}/kinematics.conf"
install -m 644 "${ROOT}/deploy/config/braillatron.conf" "${CONFIG_DIR}/braillatron.conf"
install -m 644 "${ROOT}/deploy/config/library.conf" "${CONFIG_DIR}/library.conf"
install -m 644 "${ROOT}/deploy/config/localsend.conf" "${CONFIG_DIR}/localsend.conf"

install -d /var/lib/braillatron/ram
install -d /data/braillatron/documents /data/braillatron/settings /data/braillatron/vosk-models || true

install -m 644 "${ROOT}/deploy/systemd/braillatron-ui.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sentinel.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sync.service" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron-sync.timer" "${SYSTEMD_DIR}/"
install -m 644 "${ROOT}/deploy/systemd/braillatron.target" "${SYSTEMD_DIR}/"

systemctl daemon-reload
systemctl enable braillatron.target
systemctl enable braillatron-sync.timer

echo "Installed Braillatron to ${PREFIX}/bin and ${CONFIG_DIR}"
echo "Start with: systemctl start braillatron.target"
