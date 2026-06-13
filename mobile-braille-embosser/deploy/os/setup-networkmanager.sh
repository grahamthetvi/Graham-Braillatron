#!/usr/bin/env bash
# Braillatron uses NetworkManager (nmcli) for Wi-Fi. DietPi defaults to ifupdown,
# which fights NM and fails at boot when no DietPi Wi-Fi profile exists.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-networkmanager.sh must run as root." >&2
  exit 1
fi

echo "Ensuring NetworkManager owns Wi-Fi (mask DietPi ifup@wlan0)..."

systemctl enable NetworkManager 2>/dev/null || true
systemctl disable NetworkManager-wait-online.service 2>/dev/null || true
systemctl mask NetworkManager-wait-online.service 2>/dev/null || true

systemctl disable ifup@wlan0.service 2>/dev/null || true
systemctl mask ifup@wlan0.service 2>/dev/null || true

# Prefer keyfile profiles; do not let ifupdown steal wlan0 from NM.
NM_CONF="/etc/NetworkManager/NetworkManager.conf"
if [[ -f "${NM_CONF}" ]]; then
  if ! grep -q '^\[ifupdown\]' "${NM_CONF}"; then
    cat >>"${NM_CONF}" <<'EOF'

[ifupdown]
managed=false
EOF
  elif grep -q '^managed=true' "${NM_CONF}"; then
    sed -i '/^\[ifupdown\]/,/^$/ s/^managed=true/managed=false/' "${NM_CONF}"
  fi
fi

echo "NetworkManager Wi-Fi routing configured."
