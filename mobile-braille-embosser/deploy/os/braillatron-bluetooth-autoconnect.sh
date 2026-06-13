#!/usr/bin/env bash
# Reconnect a previously paired Bluetooth speaker after boot (non-blocking).
# Does not switch ALSA routing; aux remains default until braillatron-audio-select.
set -euo pipefail

CONF="/etc/braillatron/bluetooth-audio.conf"

if [[ ! -f "${CONF}" ]]; then
  exit 0
fi

device_mac=""
# shellcheck disable=SC1090
source "${CONF}"

if [[ -z "${device_mac:-}" ]]; then
  exit 0
fi

systemctl start bluetooth 2>/dev/null || true
sleep 2
bluetoothctl connect "${device_mac}" 2>/dev/null || true
