#!/usr/bin/env bash
# Sync current repo deploy artifacts onto a DietPi SD card mounted on a dev PC.
# Does not rebuild aarch64 binaries — run "sudo make install" on the Pi after SSH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DISK=""
MOUNT_ROOT="/mnt/pi-root"
MOUNT_DATA="/mnt/pi-data"
YES=0
LIST_ONLY=0

usage() {
  cat <<EOF
Usage: sudo $(basename "$0") [--disk /dev/sdX] [--list] [-y]

  Sync Braillatron systemd units, console scripts, and network fixes onto a
  flashed/bootstrap SD card while it is plugged into this PC.

Options:
  --disk PATH   Block device (default: auto-detect removable ~29G card)
  --list        Show candidate disks and exit
  -y            Skip confirmation prompt
EOF
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --disk)
      DISK="${2:-}"
      shift 2
      ;;
    --list)
      LIST_ONLY=1
      shift
      ;;
    -y)
      YES=1
      shift
      ;;
    -h|--help)
      usage
      ;;
    *)
      usage
      ;;
  esac
done

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Must run as root (sudo)." >&2
  exit 1
fi

list_candidates() {
  lsblk -dpno NAME,SIZE,TYPE,RM | awk '$3=="disk" && $4==1 {print}'
}

pick_disk() {
  if [[ -n "${DISK}" ]]; then
    echo "${DISK}"
    return
  fi
  mapfile -t candidates < <(list_candidates)
  if [[ ${#candidates[@]} -eq 0 ]]; then
    echo "No removable block devices found. Use --disk /dev/sdX" >&2
    exit 1
  fi
  if [[ ${#candidates[@]} -gt 1 ]]; then
    echo "Multiple removable disks:" >&2
    list_candidates >&2
    echo "Use --disk to select one." >&2
    exit 1
  fi
  awk '{print $1}' <<<"${candidates[0]}"
}

if [[ "${LIST_ONLY}" -eq 1 ]]; then
  echo "Removable disks:"
  list_candidates
  exit 0
fi

DISK="$(pick_disk)"
ROOT_PART="${DISK}1"
DATA_PART="${DISK}2"

if [[ ! -b "${ROOT_PART}" ]]; then
  echo "Root partition not found: ${ROOT_PART}" >&2
  exit 1
fi

echo "Target disk:  ${DISK}"
echo "Root part:    ${ROOT_PART}"
echo "Repo:         ${REPO_ROOT}"

if [[ "${YES}" -eq 0 ]]; then
  read -r -p "Patch ${DISK}? [y/N] " reply
  [[ "${reply}" =~ ^[Yy]$ ]] || exit 0
fi

mkdir -p "${MOUNT_ROOT}" "${MOUNT_DATA}"
if ! mountpoint -q "${MOUNT_ROOT}"; then
  mount -o rw "${ROOT_PART}" "${MOUNT_ROOT}"
fi
if [[ -b "${DATA_PART}" ]] && ! mountpoint -q "${MOUNT_DATA}"; then
  mount -o rw "${DATA_PART}" "${MOUNT_DATA}" 2>/dev/null || true
fi

PI="${MOUNT_ROOT}"
SYSTEMD="${PI}/etc/systemd/system"
WANTS="${SYSTEMD}/multi-user.target.wants"

echo ""
echo "== Installing systemd units and scripts =="
install -d "${SYSTEMD}"
install -m 644 "${REPO_ROOT}/deploy/systemd/"*.service "${SYSTEMD}/"
install -m 644 "${REPO_ROOT}/deploy/systemd/"*.timer "${SYSTEMD}/"
install -m 644 "${REPO_ROOT}/deploy/systemd/braillatron.target" "${SYSTEMD}/"
install -m 755 "${REPO_ROOT}/deploy/os/braillatron-console-ready.sh" "${PI}/usr/local/sbin/braillatron-console-ready.sh"
install -m 755 "${REPO_ROOT}/deploy/os/braillatron-tty1-launch.sh" "${PI}/usr/local/sbin/braillatron-tty1-launch.sh"
install -m 755 "${REPO_ROOT}/deploy/os/braillatron-boot-diagnose.sh" "${PI}/usr/local/bin/braillatron-boot-diagnose"
install -d "${PI}/etc/systemd/system/getty@tty1.service.d"
install -m 644 "${REPO_ROOT}/deploy/systemd/getty@tty1.service.d/braillatron-appliance.conf" \
  "${PI}/etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf"

enable_unit() {
  local unit="$1"
  local src=""
  if [[ -f "${SYSTEMD}/${unit}" ]]; then
    src="${SYSTEMD}/${unit}"
  elif [[ -f "${PI}/lib/systemd/system/${unit}" ]]; then
    src="/lib/systemd/system/${unit}"
  elif [[ -f "${PI}/usr/lib/systemd/system/${unit}" ]]; then
    src="/usr/lib/systemd/system/${unit}"
  else
    echo "  skip enable (missing): ${unit}" >&2
    return
  fi
  mkdir -p "${WANTS}"
  ln -sf "${src}" "${WANTS}/${unit}"
}

mkdir -p "${WANTS}"
enable_unit braillatron.target
enable_unit braillatron-ui.service
enable_unit braillatron-ui-stub.service
rm -f "${WANTS}/braillatron-console-ready.service"
enable_unit getty@tty1.service
enable_unit braillatron-sentinel.service
enable_unit braillatron-connectd.service
enable_unit braillatron-sync.timer
enable_unit ssh.service

echo ""
echo "== Appliance / HDMI routing =="
install -d "${PI}/etc/braillatron"
if [[ ! -f "${PI}/etc/braillatron/appliance.env" ]] \
    || ! grep -q '^BRAILLATRON_HEADLESS=' "${PI}/etc/braillatron/appliance.env"; then
  cat >"${PI}/etc/braillatron/appliance.env" <<'EOF'
# Managed by patch-sd-card.sh / setup-appliance-mode.sh
BRAILLATRON_HEADLESS=0
EOF
else
  sed -i 's/^BRAILLATRON_HEADLESS=.*/BRAILLATRON_HEADLESS=0/' "${PI}/etc/braillatron/appliance.env"
fi
rm -f "${PI}/etc/braillatron/appliance-headless"
rm -f "${PI}/etc/braillatron/appliance-spi"
rm -f "${SYSTEMD}/getty@tty1.service"
rm -f "${WANTS}/braillatron-console-ui.service"

for boot_env in "${PI}/boot/dietpiEnv.txt" "${PI}/boot/firmware/dietpiEnv.txt"; do
  if [[ -f "${boot_env}" ]] && grep -q 'spi-spidev' "${boot_env}"; then
    sed -i 's/ spi-spidev//g; s/spi-spidev //g; s/^overlays=spi-spidev$/overlays=/' "${boot_env}"
    echo "  removed stale spi-spidev from ${boot_env}"
  fi
done

echo ""
echo "== fstab tmpfs dedupe (setup-overlay-ro.sh logic) =="
TMPFS_MP_RE='^[[:space:]]*tmpfs[[:space:]]+/(tmp|var/tmp|var/log|var/lib/braillatron)[[:space:]]'
TMPFS_HDR_RE='^# braillatron volatile tmpfs'
fstab_tmp="$(mktemp)"
grep -vE "${TMPFS_MP_RE}|${TMPFS_HDR_RE}" "${PI}/etc/fstab" >"${fstab_tmp}"
cat >>"${fstab_tmp}" <<'EOF'
# braillatron volatile tmpfs (setup-overlay-ro.sh)
tmpfs /tmp tmpfs size=256M,noatime,nodev,nosuid,mode=1777 0 0
tmpfs /var/tmp tmpfs size=64M,noatime,nodev,nosuid,mode=1777 0 0
tmpfs /var/log tmpfs size=50M,noatime,nodev,nosuid,mode=0755 0 0
tmpfs /var/lib/braillatron tmpfs size=128M,noatime,nodev,nosuid,mode=0755 0 0
EOF
awk '$2=="/" { gsub(/,rw/, ",ro"); gsub(/ rw /, " ro "); sub(/ rw$/, " ro"); sub(/^([^ ]+ [^ ]+ [^ ]+ )rw /, "\\1ro ") }1' \
  "${fstab_tmp}" >"${fstab_tmp}.ro"
install -m 644 "${fstab_tmp}.ro" "${PI}/etc/fstab"
rm -f "${fstab_tmp}" "${fstab_tmp}.ro"

echo ""
echo "== Quick checks on mounted root =="
checks=0
if [[ -x "${PI}/usr/bin/figlet" || -x "${PI}/usr/local/bin/figlet" ]]; then
  echo "  OK  figlet installed"
else
  echo "  note: figlet not installed — banner uses ASCII art until apt install figlet on Pi"
fi
if [[ -x "${PI}/usr/local/sbin/braillatron-tty1-launch.sh" ]]; then
  echo "  OK  tty1 launch script"
else
  echo "  MISSING tty1 launch script"
  checks=1
fi
if [[ -x "${PI}/usr/bin/openvt" ]]; then
  echo "  note: openvt present (legacy; not used by current UI path)"
else
  echo "  note: openvt missing — not required for framebuffer UI"
fi
grep -q 'SupplementaryGroups=input video' "${SYSTEMD}/braillatron-ui.service" && echo "  OK  braillatron-ui video group" || { echo "  MISSING video group in braillatron-ui.service"; checks=1; }
grep -q "Graham Braillatron" "${PI}/usr/local/sbin/braillatron-console-ready.sh" && echo "  OK  console-ready banner script" || { echo "  MISSING banner script"; checks=1; }
if [[ -L "${SYSTEMD}/ifup@wlan0.service" ]] && [[ "$(readlink "${SYSTEMD}/ifup@wlan0.service")" == "/dev/null" ]]; then
  echo "  WARN  ifup@wlan0 masked — unmask on Pi: systemctl unmask ifup@wlan0.service"
  checks=1
else
  echo "  OK  ifup@wlan0 not masked (DietPi ifupdown)"
fi
if grep -q 'Managed by NetworkManager' "${PI}/etc/network/interfaces" 2>/dev/null; then
  echo "  WARN  /etc/network/interfaces stubbed for NM — restore interfaces.braillatron.bak on Pi"
  checks=1
else
  echo "  OK  /etc/network/interfaces not NM-stubbed"
fi
[[ ! -f "${PI}/etc/braillatron/appliance-headless" ]] && echo "  OK  HDMI UI not headless" || { echo "  HEADLESS flag present"; checks=1; }
if [[ -f "${PI}/etc/systemd/system/getty@tty1.service.d/braillatron-appliance.conf" ]]; then
  echo "  OK  getty@tty1 braillatron drop-in installed"
else
  echo "  MISSING getty drop-in"
  checks=1
fi
if [[ -L "${SYSTEMD}/getty@tty1.service" ]] && [[ "$(readlink "${SYSTEMD}/getty@tty1.service")" == "/dev/null" ]]; then
  echo "  WARN  getty@tty1 still masked — unmask on Pi or re-run setup-appliance-mode.sh"
  checks=1
elif [[ -L "${WANTS}/getty@tty1.service" ]]; then
  echo "  OK  getty@tty1 enabled for multi-user boot"
else
  echo "  note: getty@tty1 not in multi-user.target.wants"
fi
if [[ -x "${PI}/usr/local/bin/braillatron-ui" ]]; then
  echo "  note: braillatron-ui binary present (rebuild on Pi for latest C++ fixes)"
else
  echo "  warn: /usr/local/bin/braillatron-ui missing — bootstrap may be incomplete"
fi

echo ""
if [[ "${checks}" -eq 0 ]]; then
  echo "Patch complete. Safe to unmount and boot the Pi."
else
  echo "Patch finished with warnings — review output above."
fi
echo ""
echo "After boot (USB keyboard on Pi): Network and Devices → join Wi-Fi."
echo "Factory Wi-Fi over SSH (before reboot): sudo bash deploy/os/setup-wifi-credentials.sh SSID PASS"
echo "After SSH: cd mobile-braille-embosser && sudo make install  # pick up C++ changes"
echo ""
echo "Unmount:"
echo "  sync && sudo umount ${MOUNT_DATA} ${MOUNT_ROOT}"
