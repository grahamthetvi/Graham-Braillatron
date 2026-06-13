#!/usr/bin/env bash
# Appliance mode: clear tty1 boot scroll and show a ready banner.
# --no-wait: print immediately (console-ui ExecStartPre, before ncurses).
# Default: wait for braillatron-ui then print (SPI headless path).
set -euo pipefail

CONSOLE="/dev/tty1"
MAX_WAIT=120
POLL_SEC=2
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

wait_for_ui() {
  local elapsed=0
  while (( elapsed < MAX_WAIT )); do
    if systemctl is-active --quiet braillatron-ui \
        || systemctl is-active --quiet braillatron-console-ui \
        || systemctl is-active --quiet braillatron-ui-stub; then
      return 0
    fi
    sleep "${POLL_SEC}"
    elapsed=$((elapsed + POLL_SEC))
  done
  return 1
}

print_banner() {
  # Linux console on tty1 — figlet and follow-on ncurses need a real TERM type.
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

if (( NO_WAIT == 0 )); then
  wait_for_ui || true
fi

print_banner >"${CONSOLE}" 2>/dev/null || true
