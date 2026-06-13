#!/usr/bin/env bash
# Braillatron uses NetworkManager (nmcli) for Wi-Fi and Ethernet.
# DietPi defaults to ifupdown in /etc/network/interfaces — running both stacks
# leaves eth0 unmanaged or stuck (ifup@eth0 vs NetworkManager conflict).
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-networkmanager.sh must run as root." >&2
  exit 1
fi

mask_ifup_iface() {
  local iface="$1"
  systemctl disable "ifup@${iface}.service" 2>/dev/null || true
  systemctl mask "ifup@${iface}.service" 2>/dev/null || true
}

detect_wired_iface() {
  local iface=""
  iface="$(nmcli -t -f DEVICE,TYPE dev status 2>/dev/null | awk -F: '$2=="ethernet" && $1!="lo" { print $1; exit }')" || true
  if [[ -n "${iface}" ]]; then
    echo "${iface}"
    return
  fi
  for candidate in end0 eth0 enp1s0; do
    if [[ -d "/sys/class/net/${candidate}" ]]; then
      echo "${candidate}"
      return
    fi
  done
  for candidate in /sys/class/net/en*; do
    [[ -e "${candidate}" ]] || continue
    iface="${candidate##*/}"
    [[ "${iface}" == "lo" ]] && continue
    echo "${iface}"
    return
  done
}

echo "Migrating network stack to NetworkManager (Wi-Fi + Ethernet)..."

systemctl enable NetworkManager 2>/dev/null || true
systemctl disable NetworkManager-wait-online.service 2>/dev/null || true
systemctl mask NetworkManager-wait-online.service 2>/dev/null || true

# Stop ifupdown from owning wlan0/eth0 — NM manages both after interfaces stub.
mask_ifup_iface wlan0
mask_ifup_iface eth0
mask_ifup_iface end0

INTERFACES="/etc/network/interfaces"
if [[ -f "${INTERFACES}" ]] && ! grep -q 'Managed by NetworkManager' "${INTERFACES}"; then
  if [[ ! -f "${INTERFACES}.braillatron.bak" ]]; then
    cp -a "${INTERFACES}" "${INTERFACES}.braillatron.bak"
    echo "Backed up ${INTERFACES} -> ${INTERFACES}.braillatron.bak"
  fi
fi
cat >"${INTERFACES}" <<'EOF'
# Managed by NetworkManager (deploy/os/setup-networkmanager.sh).
# Physical interfaces (Ethernet, Wi-Fi) are not listed here so NM owns them.
# Restore DietPi ifupdown: sudo cp /etc/network/interfaces.braillatron.bak /etc/network/interfaces
auto lo
iface lo inet loopback
EOF

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

WIRED_IF="$(detect_wired_iface)"
if [[ -n "${WIRED_IF}" ]]; then
  if ! nmcli -t -f NAME con show 2>/dev/null | grep -qx 'Braillatron-Ethernet'; then
    nmcli con add type ethernet con-name "Braillatron-Ethernet" ifname "${WIRED_IF}" \
      ipv4.method auto ipv6.method auto autoconnect yes 2>/dev/null || true
  else
    nmcli con mod "Braillatron-Ethernet" connection.interface-name "${WIRED_IF}" \
      connection.autoconnect yes ipv4.method auto 2>/dev/null || true
  fi
  nmcli dev connect "${WIRED_IF}" 2>/dev/null || true
  echo "Ethernet (${WIRED_IF}): NetworkManager autoconnect profile ready."
else
  echo "No wired interface detected yet — Braillatron-Ethernet profile will apply on next boot."
fi

systemctl restart NetworkManager 2>/dev/null || true

echo "NetworkManager owns Wi-Fi and Ethernet (ifupdown stubbed to loopback only)."
