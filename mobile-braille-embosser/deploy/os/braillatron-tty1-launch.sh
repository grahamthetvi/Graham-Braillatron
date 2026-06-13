#!/usr/bin/env bash
# Appliance HDMI console: always attach something to tty1.
# Replaces masked-getty + openvt — if the UI cannot start, this script still
# prints a diagnostic message instead of leaving the screen frozen at boot logs.
set -euo pipefail

CONSOLE="/dev/tty1"

write_tty() {
  if [[ -c "${CONSOLE}" ]]; then
    printf '%s\n' "$@" >"${CONSOLE}" 2>/dev/null || true
  fi
}

show_error() {
  write_tty \
    '' \
    '  Braillatron HDMI console failed to start' \
    '' \
    "$@" \
    '' \
    '  SSH in and run: sudo braillatron-boot-diagnose.sh' \
    '' \
    '  Common fixes:' \
    '    sudo bash deploy/bootstrap-dietpi.sh   # full install' \
    '    sudo bash deploy/os/setup-appliance-mode.sh && sudo reboot' \
    ''
}

if [[ -f /etc/braillatron/appliance-spi ]]; then
  /usr/local/sbin/braillatron-console-ready.sh --no-wait || true
  write_tty '  SPI panel mode — UI runs on the HAT display.'
  exec tail -f /dev/null
fi

if [[ -f /etc/braillatron/appliance-headless ]]; then
  /usr/local/sbin/braillatron-console-ready.sh --no-wait || true
  write_tty '  Headless mode — listen for TTS. SSH is available for maintenance.'
  exec tail -f /dev/null
fi

if [[ ! -x /usr/local/bin/braillatron-ui ]]; then
  show_error \
    '  /usr/local/bin/braillatron-ui is missing.' \
    '  Bootstrap was not completed on this SD card.'
  exec tail -f /dev/null
fi

if ! command -v speech-dispatcher >/dev/null 2>&1; then
  show_error '  speech-dispatcher is not installed.'
  exec tail -f /dev/null
fi

/usr/local/sbin/braillatron-console-ready.sh --no-wait || true

export TERM="${TERM:-linux}"
export BRAILLATRON_CONFIG="${BRAILLATRON_CONFIG:-/etc/braillatron}"
export SPEECHD_ADDRESS="${SPEECHD_ADDRESS:-unix_socket:/run/speech-dispatcher/speechd.sock}"

exec /usr/local/bin/braillatron-ui "${BRAILLATRON_CONFIG}/hardware.conf"
