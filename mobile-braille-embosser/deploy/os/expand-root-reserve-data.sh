#!/usr/bin/env bash
set -euo pipefail

# Grow the DietPi root partition to use all space before the /data tail.
# Run on the Pi when root is still the small flashed size (skip file worked)
# but DietPi-Upgrade or bootstrap needs more room on /.

DATA_TAIL_MB=768
TAIL_MARGIN_MB=32

if [[ "$(id -u)" -ne 0 ]]; then
  echo "expand-root-reserve-data.sh must run as root (sudo)." >&2
  exit 1
fi

DISK="$(findmnt -n -o SOURCE / | sed 's/p[0-9]*$//')"
ROOT_SRC="$(findmnt -n -o SOURCE /)"
if [[ -z "${DISK}" || -z "${ROOT_SRC}" ]]; then
  echo "Unable to detect root disk." >&2
  exit 1
fi

ROOT_PART_NUM="$(echo "${ROOT_SRC}" | sed -E 's/.*p?([0-9]+)$/\1/')"
if [[ -z "${ROOT_PART_NUM}" || "${ROOT_PART_NUM}" -eq 0 ]]; then
  echo "Unable to detect root partition number from ${ROOT_SRC}." >&2
  exit 1
fi

disk_end_mb() {
  parted -ms "${DISK}" unit MB print | awk -F: -v disk="${DISK}" '
    $1 == disk {
      gsub(/MB/, "", $2)
      print int($2)
      exit
    }
  '
}

part_field_mb() {
  local num="$1"
  local field="$2"
  parted -ms "${DISK}" unit MB print | awk -F: -v n="${num}" -v f="${field}" '
    $1 == n {
      gsub(/MB/, "", $f)
      print int($f)
      exit
    }
  '
}

tail_free_mb() {
  parted -ms "${DISK}" unit MB print free | awk -F: '
    $NF ~ /^free/ {
      gsub(/MB/, "", $4)
      tail = $4
    }
    END { print int(tail) }
  '
}

DISK_END_MB="$(disk_end_mb)"
CURRENT_END_MB="$(part_field_mb "${ROOT_PART_NUM}" 3)"
TARGET_END_MB=$((DISK_END_MB - DATA_TAIL_MB - TAIL_MARGIN_MB))

DATA_DEV="$(blkid -L braillatron-data 2>/dev/null || true)"
if [[ -n "${DATA_DEV}" ]]; then
  DATA_PART_NUM="$(basename "${DATA_DEV}" | sed -E 's/.*p?([0-9]+)$/\1/')"
  DATA_START_MB="$(part_field_mb "${DATA_PART_NUM}" 2)"
  if [[ -n "${DATA_START_MB}" && "${DATA_START_MB}" -gt 0 ]]; then
    TARGET_END_MB=$((DATA_START_MB - 1))
  fi
fi

if [[ "${TARGET_END_MB}" -le "${CURRENT_END_MB}" ]]; then
  echo "Root partition already ends at ${CURRENT_END_MB} MB (target <= ${TARGET_END_MB} MB)."
  df -h /
  exit 0
fi

TAIL_MB="$(tail_free_mb)"
if [[ -z "${DATA_DEV}" && ( -z "${TAIL_MB}" || "${TAIL_MB}" -lt "${DATA_TAIL_MB}" ) ]]; then
  echo "Not enough tail space (${TAIL_MB:-0} MB) to reserve ${DATA_TAIL_MB} MB for /data." >&2
  echo "Re-flash and run deploy/prepare-sd-card.py, or shrink root from a PC." >&2
  exit 1
fi

echo "Disk:        ${DISK}"
echo "Root:        ${ROOT_SRC} (partition ${ROOT_PART_NUM})"
echo "Growing:     ${CURRENT_END_MB} MB -> ${TARGET_END_MB} MB"
echo "Tail free:   ${TAIL_MB:-unknown} MB (reserving ${DATA_TAIL_MB} MB for /data)"
echo "Before:"
df -h /

parted "${DISK}" --script -- resizepart "${ROOT_PART_NUM}" "${TARGET_END_MB}MB"
partprobe "${DISK}" || true
sleep 1
resize2fs "${ROOT_SRC}"

echo "After:"
df -h /
echo "Root expanded. Safe to retry DietPi-Upgrade or bootstrap."
