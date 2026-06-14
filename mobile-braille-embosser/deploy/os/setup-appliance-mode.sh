#!/usr/bin/env bash
# Production appliance lockdown: boot straight into braillatron-ui, no local login,
# read-only root. SSH remains enabled for development and maintenance.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

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

echo "Configuring HDMI console (getty@tty1 runs Braillatron or diagnostics)..."
GETTY_DROPIN_DIR="/etc/systemd/system/getty@tty1.service.d"
GETTY_DROPIN="${GETTY_DROPIN_DIR}/braillatron-appliance.conf"
install -d "${GETTY_DROPIN_DIR}"
install -m 644 "${REPO_ROOT}/deploy/systemd/getty@tty1.service.d/braillatron-appliance.conf" "${GETTY_DROPIN}"
install -m 755 "${SCRIPT_DIR}/braillatron-tty1-launch.sh" /usr/local/sbin/braillatron-tty1-launch.sh
install -m 755 "${SCRIPT_DIR}/braillatron-boot-diagnose.sh" /usr/local/bin/braillatron-boot-diagnose
systemctl unmask getty@tty1.service 2>/dev/null || true
systemctl enable getty@tty1.service 2>/dev/null || true

echo "Disabling serial console login (appliance mode)..."
while IFS= read -r unit; do
  [[ -n "${unit}" ]] || continue
  systemctl disable --now "${unit}" 2>/dev/null || true
done < <(systemctl list-unit-files 'serial-getty@*.service' --no-legend 2>/dev/null | awk '{print $1}')
systemctl mask serial-getty@.service 2>/dev/null || true

echo "Suppressing DietPi console login banner..."
AUTOLOGIN_DROPIN="${GETTY_DROPIN_DIR}/dietpi-autologin.conf"
cat >"${AUTOLOGIN_DROPIN}" <<'EOF'
# Braillatron appliance mode: DietPi postboot skips the login banner when this file exists.
EOF
echo "DietPi banner suppression: ${AUTOLOGIN_DROPIN}"

# Belt-and-suspenders: postboot prints the banner when the drop-in is missing.
# Masking avoids a stale/misleading prompt if getty is disabled.
systemctl disable dietpi-postboot.service 2>/dev/null || true
systemctl mask dietpi-postboot.service 2>/dev/null || true

HEADLESS="${BRAILLATRON_HEADLESS:-0}"
SPI_PANEL="${BRAILLATRON_SPI_PANEL:-0}"
install -d /etc/braillatron
cat >/etc/braillatron/appliance.env <<EOF
# Managed by setup-appliance-mode.sh — BRAILLATRON_HEADLESS=1 forces TTS-only (no visual UI).
BRAILLATRON_HEADLESS=${HEADLESS}
EOF
if [[ "${HEADLESS}" == "1" ]]; then
  touch /etc/braillatron/appliance-headless
else
  rm -f /etc/braillatron/appliance-headless
fi
if [[ "${SPI_PANEL}" == "1" ]]; then
  touch /etc/braillatron/appliance-spi
else
  rm -f /etc/braillatron/appliance-spi
  if [[ -f /boot/dietpiEnv.txt ]] && grep -q 'spi-spidev' /boot/dietpiEnv.txt; then
    sed -i 's/ spi-spidev//g; s/spi-spidev //g; s/^overlays=spi-spidev$/overlays=/' /boot/dietpiEnv.txt
    echo "Removed stale spi-spidev overlay (no SPI panel configured)."
  fi
fi
echo "Appliance env: BRAILLATRON_HEADLESS=${HEADLESS} BRAILLATRON_SPI_PANEL=${SPI_PANEL}"

echo "Installing display routing (SPI + HDMI framebuffer / headless stub)..."
install -m 755 "${SCRIPT_DIR}/braillatron-console-ready.sh" /usr/local/sbin/braillatron-console-ready.sh
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron-console-ready.service" /etc/systemd/system/
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron-console-ui.service" /etc/systemd/system/
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron-ui-stub.service" /etc/systemd/system/
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron-ui.service" /etc/systemd/system/
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron.target" /etc/systemd/system/
systemctl daemon-reload
systemctl enable braillatron-ui.service
systemctl disable braillatron-console-ui.service 2>/dev/null || true
systemctl mask braillatron-console-ui.service 2>/dev/null || true
systemctl enable braillatron-ui-stub.service
systemctl disable braillatron-console-ready.service 2>/dev/null || true

echo "Configuring read-only root and volatile tmpfs mounts..."
bash "${SCRIPT_DIR}/setup-overlay-ro.sh"

echo "Locking root filesystem read-only..."
cd /
sync
if ! mount -o remount,ro /; then
  echo "Note: could not remount / read-only (mount point busy). Root stays writable until reboot." >&2
  echo "  After reboot, fstab mounts / read-only and tmpfs volatile paths apply." >&2
fi

cat <<EOF

Appliance mode configured.
  - Power on: Braillatron starts automatically (no login required)
  - Visual UI: braillatron-ui.service (HDMI /dev/fb0 and/or SPI panel when configured)
  - tty1: cleared on success; UI on framebuffer/panel (unless BRAILLATRON_HEADLESS=1)
  - HDMI blank? SSH: sudo braillatron-boot-diagnose.sh
  - SSH: enabled for development and maintenance

Maintenance over SSH:
  ssh dietpi@<device-ip>
  sudo braillatron-remount-rw          # unlock / for system changes
  sudo systemctl restart braillatron.target
  sudo braillatron-remount-ro          # lock / when done

/data/braillatron/ is always writable (documents, settings, credentials).

Reboot to run the locked production image:
  reboot
EOF
