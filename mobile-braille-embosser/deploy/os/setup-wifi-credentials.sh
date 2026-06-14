#!/usr/bin/env bash
# Factory helper: pre-provision Wi-Fi credentials for DietPi wpa_supplicant.
# Usage: setup-wifi-credentials.sh SSID PASS
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-wifi-credentials.sh must run as root." >&2
  exit 1
fi

if [[ $# -ne 2 ]]; then
  echo "Usage: setup-wifi-credentials.sh SSID PASS" >&2
  exit 1
fi

SSID="$1"
PASS="$2"
CONF="/etc/wpa_supplicant/wpa_supplicant.conf"

if ! command -v wpa_passphrase &>/dev/null; then
  echo "wpa_passphrase not found — install wpasupplicant." >&2
  exit 1
fi

if [[ -f "${CONF}" ]] && grep -qF "ssid=\"${SSID}\"" "${CONF}"; then
  echo "SSID \"${SSID}\" already in ${CONF} — skipping."
  exit 0
fi

install -d -m 755 /etc/wpa_supplicant
if [[ ! -f "${CONF}" ]]; then
  cat >"${CONF}" <<'EOF'
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=US
EOF
  chmod 600 "${CONF}"
elif ! grep -q '^ctrl_interface=' "${CONF}"; then
  sed -i '1i ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' "${CONF}"
fi

wpa_passphrase "${SSID}" "${PASS}" >>"${CONF}"
chmod 600 "${CONF}"
echo "Added network block for \"${SSID}\" to ${CONF}."

if ip link show wlan0 &>/dev/null && wpa_cli -i wlan0 ping &>/dev/null; then
  wpa_cli -i wlan0 reconfigure 2>/dev/null || true
  echo "wpa_supplicant reconfigured on wlan0."
else
  echo "Run: sudo ifup wlan0   (or reboot) to connect."
fi
