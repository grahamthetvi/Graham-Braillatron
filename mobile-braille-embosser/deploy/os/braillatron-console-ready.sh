#!/usr/bin/env bash
# Shared tty1 helpers and optional ready banner (manual / SSH).
# Default CLI: wait for braillatron-ui then print figlet banner.
# Source this file for wait/clear/backend helpers (tty1-launch).
set -euo pipefail

CONSOLE="/dev/tty1"
MAX_WAIT=120
POLL_SEC=2

clear_tty1() {
  if [[ -c "${CONSOLE}" ]]; then
    printf '\033[H\033[2J\033[?25l' >"${CONSOLE}" 2>/dev/null || true
  fi
}

blank_tty1_cursor() {
  if [[ -c "${CONSOLE}" ]] && command -v setterm >/dev/null 2>&1; then
    setterm -blank force -powerdown 0 -term linux </dev/tty1 >/dev/tty1 2>/dev/null || true
  fi
}

read_display_backend() {
  local line backend
  line="$(journalctl -u braillatron-ui -b -o cat 2>/dev/null \
    | grep '\[display\] backend=' | tail -1 || true)"
  if [[ -z "${line}" ]]; then
    return 1
  fi
  backend="${line##*backend=}"
  printf '%s' "${backend}"
}

wait_for_ui_service() {
  local unit="${1:-braillatron-ui}"
  local elapsed=0

  while (( elapsed < MAX_WAIT )); do
    if systemctl is-failed --quiet "${unit}"; then
      return 2
    fi
    if systemctl is-active --quiet "${unit}"; then
      return 0
    fi
    sleep "${POLL_SEC}"
    elapsed=$((elapsed + POLL_SEC))
  done
  return 1
}

wait_for_any_ui_service() {
  local elapsed=0

  while (( elapsed < MAX_WAIT )); do
    if systemctl is-failed --quiet braillatron-ui; then
      return 2
    fi
    if systemctl is-active --quiet braillatron-ui \
        || systemctl is-active --quiet braillatron-ui-stub; then
      return 0
    fi
    sleep "${POLL_SEC}"
    elapsed=$((elapsed + POLL_SEC))
  done
  return 1
}

print_banner() {
  # Linux console on tty1 — figlet needs a real TERM type.
  export TERM="${TERM:-linux}"

  printf '\033[H\033[2J\033[?25l'
  printf '\033[1;36m'

  if command -v figlet >/dev/null 2>&1; then
    local figlet_font="slant"
    if ! figlet -I2 2>/dev/null | grep -qx "${figlet_font}"; then
      figlet_font="standard"
    fi
    figlet -f "${figlet_font}" 'Graham Braillatron' 2>/dev/null \
      || figlet 'Graham Braillatron' 2>/dev/null \
      || printf '  Graham Braillatron\n'
  else
    cat <<'EOF'
     ____                       _              _
    |  _ \ __ _ _ __ _ __ _   _| |_ _ __ _   _| |_ _   _ _ __ ___
    | |_) / _` | '__| '_ \ | | | __| '__| | | | __| | | | '__/ __|
    |  _ < (_| | |  | | | |_| | |_| |  | |_| | |_| |_| | |  \__ \
    |_| \_\__,_|_|  |_|  \__,_|\__|_|   \__,_|\__|\__,_|_|   |___/

      Graham Braillatron
EOF
  fi

  printf '\033[0m\n'
  printf '\033[1;32m  ● Braillatron ready\033[0m\n'
  printf '    Use the physical keyboard and listen for speech.\n'
  printf '    SSH is available for development.\n\n'
  printf '\033[90m  Appliance mode — no local login on this screen.\033[0m\n'
  printf '\033[?25h'
}

if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
  return 0 2>/dev/null || exit 0
fi

NO_WAIT=0

usage() {
  echo "Usage: $(basename "$0") [--no-wait]" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-wait)
      NO_WAIT=1
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

if [[ ! -c "${CONSOLE}" ]]; then
  exit 0
fi

if (( NO_WAIT == 0 )); then
  wait_for_any_ui_service
fi

print_banner >"${CONSOLE}" 2>/dev/null || true
