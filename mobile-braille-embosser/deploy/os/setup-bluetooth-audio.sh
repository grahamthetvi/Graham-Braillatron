#!/usr/bin/env bash
# Optional Bluetooth speaker support for Braillatron (BlueALSA, system-wide).
# Default output stays aux until you run: sudo braillatron-audio-select bluetooth
#
# Usage:
#   sudo bash setup-bluetooth-audio.sh              # interactive pairing
#   sudo bash setup-bluetooth-audio.sh AA:BB:...    # known MAC, skip pairing
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../apt-retry.sh
source "${ROOT}/apt-retry.sh"
BT_CONF="/etc/braillatron/bluetooth-audio.conf"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-bluetooth-audio.sh must run as root (sudo)." >&2
  exit 1
fi

MAC="${1:-}"

echo "Installing Bluetooth audio packages..."
apt_retry_update
apt_retry_install bluez bluez-tools bluez-alsa-utils

install -d /etc/braillatron
install -m 755 "${ROOT}/os/braillatron-audio-select.sh" /usr/local/bin/braillatron-audio-select
install -d /usr/share/braillatron/audio
install -m 644 "${ROOT}/os/audio/asound.aux.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/os/audio/asound.bluetooth.conf" /usr/share/braillatron/audio/
install -m 644 "${ROOT}/os/audio/asound.i2s.conf" /usr/share/braillatron/audio/

if [[ -f /etc/default/bluealsa ]]; then
  if ! grep -q '^BLUEALSA_OPTS=' /etc/default/bluealsa; then
    echo 'BLUEALSA_OPTS="-p a2dp-sink"' >> /etc/default/bluealsa
  elif ! grep -q 'a2dp-sink' /etc/default/bluealsa; then
    sed -i 's/^BLUEALSA_OPTS=.*/& -p a2dp-sink/' /etc/default/bluealsa || true
  fi
fi

systemctl enable --now bluetooth
systemctl enable --now bluealsa

if [[ -z "${MAC}" ]]; then
  echo ""
  echo "Pair your speaker now (put it in pairing mode)."
  echo "When scan shows your device, note its MAC address."
  echo ""
  read -r -p "Press Enter to open bluetoothctl (type 'quit' when done)... "
  bluetoothctl <<'EOF' || true
power on
agent on
default-agent
scan on
EOF
  echo ""
  read -r -p "Enter Bluetooth MAC (AA:BB:CC:DD:EE:FF): " MAC
fi

if [[ ! "${MAC}" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then
  echo "Invalid MAC: ${MAC}" >&2
  exit 1
fi

bluetoothctl trust "${MAC}" 2>/dev/null || true
bluetoothctl connect "${MAC}" 2>/dev/null || true

cat > "${BT_CONF}" <<EOF
# Paired Bluetooth speaker for braillatron-audio-select bluetooth
device_mac=${MAC}
EOF
chmod 644 "${BT_CONF}"

echo ""
echo "Bluetooth speaker saved: ${MAC}"
echo ""
echo "Aux remains the default until you switch:"
echo "  sudo braillatron-audio-select bluetooth"
echo "  spd-say 'Braillatron Bluetooth test'"
echo ""
echo "Switch back to aux jack anytime:"
echo "  sudo braillatron-audio-select aux"
