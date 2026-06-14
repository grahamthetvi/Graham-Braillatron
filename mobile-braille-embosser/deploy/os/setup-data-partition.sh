#!/usr/bin/env bash
set -euo pipefail

# Adds an ext4 /data partition on SD card (prototype layout).
# WARNING: Review disk layout before running on a flashed image.

if [[ "$(id -u)" -ne 0 ]]; then
  echo "setup-data-partition.sh must run as root." >&2
  exit 1
fi

DISK="$(findmnt -n -o SOURCE / | sed 's/p[0-9]*$//')"
if [[ -z "${DISK}" ]]; then
  echo "Unable to detect root disk." >&2
  exit 1
fi

if mountpoint -q /data; then
  echo "/data already mounted; skipping partition creation."
  exit 0
fi

partprobe "${DISK}" 2>/dev/null || true

find_braillatron_data_part() {
  local part label
  part="$(blkid -L braillatron-data 2>/dev/null || true)"
  if [[ -n "${part}" ]]; then
    echo "${part}"
    return 0
  fi
  if [[ -e /dev/disk/by-label/braillatron-data ]]; then
    readlink -f /dev/disk/by-label/braillatron-data
    return 0
  fi
  for part in "${DISK}"p*; do
    [[ -b "${part}" ]] || continue
    label="$(blkid -s LABEL -o value "${part}" 2>/dev/null || true)"
    if [[ "${label}" == "braillatron-data" ]]; then
      echo "${part}"
      return 0
    fi
  done
  return 1
}

ensure_braillatron_data_fs() {
  local part="$1"
  local label fstype

  label="$(blkid -s LABEL -o value "${part}" 2>/dev/null || true)"
  if [[ "${label}" == "braillatron-data" ]]; then
    echo "Partition ${part} already formatted as braillatron-data; skipping mkfs."
    return 0
  fi

  if blkid "${part}" &>/dev/null; then
    fstype="$(blkid -s TYPE -o value "${part}" 2>/dev/null || true)"
    if [[ "${fstype}" == "ext4" && -z "${label}" ]]; then
      echo "Labeling existing ext4 on ${part} as braillatron-data..."
      e2label "${part}" braillatron-data
      return 0
    fi
    echo "Partition ${part} already contains a ${fstype:-unknown} filesystem (label=${label:-none})." >&2
    echo "Refusing to overwrite; reuse the existing braillatron-data partition or wipe it manually." >&2
    exit 1
  fi

  echo "Formatting ${part} as ext4 (braillatron-data)..."
  mkfs.ext4 -F -L braillatron-data "${part}"
}

# Re-run guard: a previously created data partition must never be re-created
# or reformatted (mkpart on the disk tail is destructive).
if EXISTING="$(find_braillatron_data_part)"; then
  echo "Found existing braillatron-data partition at ${EXISTING}; reusing it."
  NEXT_PART="${EXISTING}"
else
  # Highest existing partition number + 1 (lsblk line counting is off by one:
  # it also counts the disk itself).
  LAST_PART_NUM="$(parted -ms "${DISK}" print 2>/dev/null | awk -F: '/^[0-9]+:/ {n=$1} END {print n+0}')"
  NEXT_PART="${DISK}p$((LAST_PART_NUM + 1))"

  if [[ -b "${NEXT_PART}" ]]; then
    # Partition slot already present (re-bootstrap); format only if needed.
    ensure_braillatron_data_fs "${NEXT_PART}"
  else
    # mkpart needs ~768MB of unallocated space at the end of the disk. DietPi
    # expands the root partition to fill the card on first boot, so verify the
    # gap exists instead of failing halfway through.
    FREE_TAIL_MB="$(parted -ms "${DISK}" unit MB print free 2>/dev/null \
      | awk -F: '$NF ~ /^free/ {gsub(/MB/, "", $4); tail=$4} END {print int(tail)}')"
    if [[ -z "${FREE_TAIL_MB}" || "${FREE_TAIL_MB}" -lt 768 ]]; then
      echo "Not enough unallocated space at end of ${DISK} (need 768MB, found ${FREE_TAIL_MB:-0}MB)." >&2
      echo "Shrink the root partition first, or disable DietPi auto-expand before first boot." >&2
      exit 1
    fi

    echo "Creating 768MB /data partition ${NEXT_PART} on ${DISK}..."
    parted "${DISK}" --script -- mkpart primary ext4 -768MB 100%
    partprobe "${DISK}" || true
    sleep 2

    ensure_braillatron_data_fs "${NEXT_PART}"
  fi
fi

mkdir -p /data
if ! grep -q '/data' /etc/fstab; then
  echo "LABEL=braillatron-data /data ext4 defaults,noatime 0 2" >> /etc/fstab
fi
mount /data

install -d /data/braillatron/documents /data/braillatron/settings /data/braillatron/vosk-models
install -d /var/lib/braillatron/ram

echo "/data partition ready."
