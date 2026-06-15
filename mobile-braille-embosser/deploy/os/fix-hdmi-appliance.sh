#!/usr/bin/env bash
# One-shot repair for blank HDMI after flash/bootstrap. Safe to re-run.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "fix-hdmi-appliance.sh must run as root (sudo)." >&2
  exit 1
fi

deploy_os_dir() {
  if [[ -f "${SCRIPT_DIR}/braillatron-systemd-wants.sh" ]]; then
    printf '%s\n' "${SCRIPT_DIR}"
  elif [[ -f /usr/local/sbin/braillatron-systemd-wants.sh ]]; then
    printf '/usr/local/sbin\n'
  else
    return 1
  fi
}

find_repo_root() {
  local candidate=""
  for candidate in \
    "${BRAILLATRON_REPO:-}" \
    "/home/dietpi/Graham Braillatron/mobile-braille-embosser" \
    "/home/dietpi/mobile-braille-embosser" \
    "/root/mobile-braille-embosser" \
    "${SCRIPT_DIR}/../.."; do
    [[ -n "${candidate}" ]] || continue
    if [[ -f "${candidate}/deploy/install.sh" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

OS_DIR="$(deploy_os_dir)"
REPO_ROOT="$(find_repo_root || true)"

if findmnt -n -o OPTIONS / | grep -q ',ro,'; then
  if [[ -x /usr/local/sbin/braillatron-remount-rw ]]; then
    braillatron-remount-rw
  else
    mount -o remount,rw /
  fi
fi

echo "== HDMI appliance repair =="

bash "${OS_DIR}/braillatron-systemd-wants.sh"

install -m 755 "${OS_DIR}/braillatron-console-ready.sh" /usr/local/sbin/braillatron-console-ready.sh
install -m 755 "${OS_DIR}/braillatron-tty1-launch.sh" /usr/local/sbin/braillatron-tty1-launch.sh
install -m 755 "${OS_DIR}/braillatron-fb-repaint.sh" /usr/local/sbin/braillatron-fb-repaint.sh
install -m 755 "${OS_DIR}/braillatron-systemd-wants.sh" /usr/local/sbin/braillatron-systemd-wants.sh
install -m 755 "${OS_DIR}/braillatron-boot-diagnose.sh" /usr/local/bin/braillatron-boot-diagnose
install -m 755 "${OS_DIR}/fix-hdmi-appliance.sh" /usr/local/sbin/fix-hdmi-appliance.sh

install -d /etc/braillatron
if [[ ! -f /etc/braillatron/appliance.env ]]; then
  cat >/etc/braillatron/appliance.env <<'EOF'
BRAILLATRON_HEADLESS=0
EOF
else
  sed -i 's/^BRAILLATRON_HEADLESS=.*/BRAILLATRON_HEADLESS=0/' /etc/braillatron/appliance.env
fi
rm -f /etc/braillatron/appliance-headless

systemctl daemon-reload
systemctl disable braillatron-ui-stub.service 2>/dev/null || true
systemctl disable braillatron-console-ui.service 2>/dev/null || true
systemctl mask braillatron-console-ui.service 2>/dev/null || true
systemctl disable braillatron-console-ready.service 2>/dev/null || true
systemctl enable braillatron.target
systemctl enable braillatron-ui.service
bash "${OS_DIR}/braillatron-systemd-wants.sh"

if [[ "${BRAILLATRON_SKIP_REBUILD:-0}" != "1" ]] \
    && [[ -n "${REPO_ROOT}" ]] && [[ -x "${REPO_ROOT}/deploy/install.sh" ]]; then
  echo "Rebuilding and reinstalling braillatron-ui (sync_chrome + TTS/STT)..."
  bash "${REPO_ROOT}/deploy/install.sh"
elif [[ "${BRAILLATRON_SKIP_REBUILD:-0}" == "1" ]]; then
  echo "Skipping rebuild (BRAILLATRON_SKIP_REBUILD=1)."
else
  echo "WARN: repo not found — updated scripts/units only; sync repo and run deploy/install.sh for C++ fixes." >&2
fi

GETTY_DROPIN="/etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf"
GETTY_DROPIN_DIR="/etc/systemd/system/getty@tty1.service.d"
if [[ ! -f "${GETTY_DROPIN}" ]] && [[ -n "${REPO_ROOT}" ]] \
    && [[ -f "${REPO_ROOT}/deploy/systemd/getty@tty1.service.d/braillatron-appliance.conf" ]]; then
  install -d "${GETTY_DROPIN_DIR}"
  install -m 644 "${REPO_ROOT}/deploy/systemd/getty@tty1.service.d/braillatron-appliance.conf" "${GETTY_DROPIN}"
  systemctl unmask getty@tty1.service 2>/dev/null || true
  systemctl enable getty@tty1.service 2>/dev/null || true
fi
if [[ -d "${GETTY_DROPIN_DIR}" ]]; then
  while IFS= read -r dropin; do
    [[ -n "${dropin}" ]] || continue
    if grep -q 'network-online' "${dropin}" 2>/dev/null; then
      echo "Removing getty network-online drop-in: ${dropin}"
      rm -f "${dropin}"
    fi
  done < <(find "${GETTY_DROPIN_DIR}" -maxdepth 1 -name '*.conf' -type f 2>/dev/null || true)
fi
if [[ -n "${REPO_ROOT}" ]] && [[ -f "${REPO_ROOT}/deploy/systemd/braillatron-fb-repaint.service" ]]; then
  install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron-fb-repaint.service" /etc/systemd/system/
  systemctl enable braillatron-fb-repaint.service 2>/dev/null || true
fi
systemctl disable dietpi-wifi-monitor.service 2>/dev/null || true
systemctl mask dietpi-wifi-monitor.service 2>/dev/null || true

systemctl daemon-reload
systemctl restart braillatron.target
systemctl restart getty@tty1.service 2>/dev/null || true

echo ""
braillatron-boot-diagnose

echo ""
echo "Repair complete. If HDMI is still blank: reboot, then re-run braillatron-boot-diagnose."
