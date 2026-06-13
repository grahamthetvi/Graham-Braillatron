#!/usr/bin/env bash
# Production appliance lockdown: boot straight into braillatron-ui, no local login,
# read-only root. SSH remains enabled for development and maintenance.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-appliance-mode.sh must run as root (sudo)." >&2
  exit 1
fi

if findmnt -n -o OPTIONS / | grep -q ',ro,'; then
  echo "Root is read-only; remounting read-write for appliance setup..."
  cd /
  mount -o remount,rw /
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

echo "Suppressing DietPi console login banner..."
AUTOLOGIN_DROPIN="/etc/systemd/system/getty@tty1.service.d/dietpi-autologin.conf"
mkdir -p "$(dirname "${AUTOLOGIN_DROPIN}")"
cat >"${AUTOLOGIN_DROPIN}" <<'EOF'
# Braillatron appliance mode: DietPi postboot skips the login banner when this file exists.
# getty@tty1 remains disabled; no local console login.
EOF
if [[ ! -f "${AUTOLOGIN_DROPIN}" ]]; then
  echo "ERROR: failed to create ${AUTOLOGIN_DROPIN}" >&2
  exit 1
fi
echo "DietPi banner suppression: ${AUTOLOGIN_DROPIN}"

# Belt-and-suspenders: postboot prints the banner when the drop-in is missing.
# Masking avoids a stale/misleading prompt if getty is disabled.
systemctl disable dietpi-postboot.service 2>/dev/null || true
systemctl mask dietpi-postboot.service 2>/dev/null || true

echo "Configuring read-only root and volatile tmpfs mounts..."
bash "${SCRIPT_DIR}/setup-overlay-ro.sh"

echo "Locking root filesystem read-only..."
cd /
sync
if ! mount -o remount,ro /; then
  echo "Note: could not remount / read-only (mount point busy). Root stays writable until reboot." >&2
  echo "  After reboot, tmpfs mounts apply and / is typically read-only from fstab." >&2
fi

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
