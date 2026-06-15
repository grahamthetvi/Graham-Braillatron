#!/usr/bin/env bash
# Appliance HDMI console: wait for UI service; blank tty1 cursor on success (never clear_tty1 — wipes fb).
set -euo pipefail

CONSOLE="/dev/tty1"
CONSOLE_READY="/usr/local/sbin/braillatron-console-ready.sh"

# shellcheck source=/usr/local/sbin/braillatron-console-ready.sh
source "${CONSOLE_READY}"

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
    ''
}

hold_tty1() {
  exec tail -f /dev/null
}

is_ok_display_backend() {
  case "$1" in
    fb|spi|mirror|ncurses)
      return 0
      ;;
    *+*)
      case "$1" in
        *fb*|*spi*|*mirror*) return 0 ;;
      esac
      ;;
  esac
  return 1
}

wait_for_display_backend() {
  local backend="" elapsed=0

  while (( elapsed < 10 )); do
    if backend="$(read_display_backend 2>/dev/null || true)" && [[ -n "${backend}" ]]; then
      printf '%s' "${backend}"
      return 0
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  return 1
}

if [[ ! -c "${CONSOLE}" ]]; then
  exit 0
fi

if [[ ! -x /usr/local/bin/braillatron-ui ]]; then
  show_error \
    '  /usr/local/bin/braillatron-ui is missing.' \
    '  Bootstrap was not completed on this SD card.'
  hold_tty1
fi

if ! command -v speech-dispatcher >/dev/null 2>&1; then
  show_error '  speech-dispatcher is not installed.'
  hold_tty1
fi

if ! systemctl is-active --quiet speech-dispatcher; then
  show_error '  speech-dispatcher is not running.'
  hold_tty1
fi

if [[ -f /etc/braillatron/appliance-headless ]]; then
  wait_rc=0
  wait_for_ui_service braillatron-ui-stub || wait_rc=$?
  case "${wait_rc}" in
    0)
      clear_tty1
      write_tty '  Headless mode — listen for TTS.'
      hold_tty1
      ;;
    1)
      show_error '  braillatron-ui-stub did not start within 120s.'
      hold_tty1
      ;;
    2)
      show_error \
        '  braillatron-ui-stub failed.' \
        '  Check: journalctl -u braillatron-ui-stub -b'
      hold_tty1
      ;;
  esac
fi

wait_rc=0
wait_for_ui_service braillatron-ui || wait_rc=$?
case "${wait_rc}" in
  0) ;;
  1)
    show_error '  braillatron-ui did not start within 120s.'
    hold_tty1
    ;;
  2)
    show_error \
      '  braillatron-ui failed.' \
      '  Check: journalctl -u braillatron-ui -b'
    hold_tty1
    ;;
esac

if ! systemctl is-active --quiet braillatron-ui; then
  show_error \
    '  braillatron-ui is not active.' \
    '  Check: journalctl -u braillatron-ui -b'
  hold_tty1
fi

backend=""
if ! backend="$(wait_for_display_backend)"; then
  show_error \
    '  UI running but no display backend — check /dev/fb0 and display.conf'
  hold_tty1
fi

if ! is_ok_display_backend "${backend}"; then
  show_error \
    "  UI running but display backend=${backend} — check display.conf and mirror snapshot path" \
    '  Over SSH run: braillatron-ui-watch'
  hold_tty1
fi

if [[ "${backend}" == *"mirror"* ]]; then
  write_tty \
    '' \
    '  Visual UI available over SSH:' \
    '    braillatron-ui-watch' \
    ''
fi

# Pixel backends draw directly on the framebuffer; ESC [2J from clear_tty1 wipes the UI.
# blank_tty1_cursor must not use setterm -blank force (that blanks HDMI via APM).
blank_tty1_cursor
hold_tty1
