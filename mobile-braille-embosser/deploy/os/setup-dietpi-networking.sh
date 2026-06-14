#!/usr/bin/env bash
# Braillatron uses DietPi ifupdown + wpa_supplicant for Wi-Fi and Ethernet.
# Disables NetworkManager if present; does not modify /etc/network/interfaces.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-dietpi-networking.sh must run as root." >&2
  exit 1
fi

unmask_ifup_iface() {
  local iface="$1"
  systemctl unmask "ifup@${iface}.service" 2>/dev/null || true
  systemctl enable "ifup@${iface}.service" 2>/dev/null || true
}

echo "Ensuring DietPi ifupdown + wpa_supplicant networking..."

if systemctl list-unit-files NetworkManager.service &>/dev/null; then
  systemctl disable --now NetworkManager 2>/dev/null || true
  systemctl disable NetworkManager-wait-online.service 2>/dev/null || true
  systemctl mask NetworkManager-wait-online.service 2>/dev/null || true
  echo "NetworkManager disabled (DietPi ifupdown owns interfaces)."
fi

unmask_ifup_iface wlan0
for wired in eth0 end0; do
  if [[ -d "/sys/class/net/${wired}" ]]; then
    unmask_ifup_iface "${wired}"
  fi
done

echo "DietPi networking ready (ifup@wlan0 enabled; interfaces file untouched)."
echo "Reboot after bootstrap to apply cleanly — do not restart networking mid-session."
