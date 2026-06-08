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

PART_COUNT="$(lsblk -ln "${DISK}" | wc -l)"
NEXT_PART="${DISK}p$((PART_COUNT + 1))"

echo "Creating 768MB /data partition on ${DISK}..."
parted "${DISK}" --script mkpart primary ext4 -768MB 100%
partprobe "${DISK}" || true
sleep 2

mkfs.ext4 -L braillatron-data "${NEXT_PART}"
mkdir -p /data
if ! grep -q '/data' /etc/fstab; then
  echo "LABEL=braillatron-data /data ext4 defaults,noatime 0 2" >> /etc/fstab
fi
mount /data

install -d /data/braillatron/documents /data/braillatron/settings /data/braillatron/vosk-models
install -d /var/lib/braillatron/ram

echo "/data partition ready."
