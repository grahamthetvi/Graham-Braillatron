#!/usr/bin/env bash
# Production appliance lockdown: boot straight into braillatron-ui, no local login,
# read-only root. SSH remains enabled for development and maintenance.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-appliance-mode.sh must run as root (sudo)." >&2
  exit 1
fi

echo "Enabling Braillatron services..."
systemctl enable braillatron.target

echo "Ensuring SSH is available for development access..."
if ! command -v sshd >/dev/null 2>&1; then
  apt-get install -y openssh-server
fi
systemctl enable ssh 2>/dev/null || systemctl enable sshd 2>/dev/null || true
systemctl start ssh 2>/dev/null || systemctl start sshd 2>/dev/null || true

echo "Disabling local console login (appliance mode)..."
for unit in getty@tty1.service; do
  systemctl disable --now "${unit}" 2>/dev/null || true
done
while IFS= read -r unit; do
  [[ -n "${unit}" ]] || continue
  systemctl disable --now "${unit}" 2>/dev/null || true
done < <(systemctl list-unit-files 'serial-getty@*.service' --no-legend 2>/dev/null | awk '{print $1}')

echo "Configuring read-only root and volatile tmpfs mounts..."
bash "${SCRIPT_DIR}/setup-overlay-ro.sh"

echo "Locking root filesystem read-only..."
sync
mount -o remount,ro /

cat <<EOF

Appliance mode configured.
  - Power on: braillatron-ui starts automatically (no login required)
  - Local console: no login prompt
  - SSH: enabled for development and maintenance

Maintenance over SSH:
  ssh dietpi@<device-ip>
  sudo braillatron-remount-rw          # unlock / for system changes
  sudo systemctl restart braillatron-ui
  sudo braillatron-remount-ro          # lock / when done

/data/braillatron/ is always writable (documents, settings, credentials).

Reboot to run the locked production image:
  reboot
EOF
