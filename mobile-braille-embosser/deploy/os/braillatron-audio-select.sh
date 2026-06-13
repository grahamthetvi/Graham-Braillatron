#!/usr/bin/env bash
# Switch Braillatron ALSA output: aux (default), bluetooth, or i2s.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d /usr/share/braillatron/audio ]]; then
  AUDIO_DIR="/usr/share/braillatron/audio"
elif [[ -d "${SCRIPT_DIR}/audio" ]]; then
  AUDIO_DIR="${SCRIPT_DIR}/audio"
else
  ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
  AUDIO_DIR="${ROOT}/os/audio"
fi

STATE_FILE="/etc/braillatron/audio-output.conf"
BT_CONF="/etc/braillatron/bluetooth-audio.conf"
ASOUND="/etc/asound.conf"
NO_RESTART_UI=0

usage() {
  echo "Usage: braillatron-audio-select [--no-restart-ui] {aux|bluetooth|i2s|status}" >&2
  exit 1
}

require_root() {
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "Must run as root (sudo)." >&2
    exit 1
  fi
}

detect_aux_card() {
  if [[ -n "${Braillatron_AUX_CARD:-}" ]]; then
    echo "${Braillatron_AUX_CARD}"
    return
  fi
  if [[ -f "${STATE_FILE}" ]]; then
    # shellcheck disable=SC1090
    source "${STATE_FILE}"
    if [[ -n "${aux_card:-}" ]]; then
      echo "${aux_card}"
      return
    fi
  fi
  # Orange Pi analog jack is usually card 0; override with Braillatron_AUX_CARD= if needed.
  echo "0"
}

apply_speech_dispatcher_alsa() {
  if [[ -f /etc/speech-dispatcher/speechd.conf ]]; then
    sed -i 's/^#* *AudioOutputMethod.*/AudioOutputMethod "alsa"/' /etc/speech-dispatcher/speechd.conf
  fi
  rm -f /etc/systemd/system/speech-dispatcher.service.d/pipewire.conf
  rmdir /etc/systemd/system/speech-dispatcher.service.d 2>/dev/null || true
  systemctl daemon-reload
  systemctl restart speech-dispatcher 2>/dev/null || true
}

write_state() {
  local mode="$1"
  local aux_card="$2"
  install -d /etc/braillatron
  cat > "${STATE_FILE}" <<EOF
# Managed by braillatron-audio-select.sh — do not edit by hand unless needed.
mode=${mode}
aux_card=${aux_card}
EOF
}

install_asound_from_template() {
  local template="$1"
  shift
  local content
  content="$(cat "${template}")"
  for pair in "$@"; do
    local key="${pair%%=*}"
    local val="${pair#*=}"
    content="${content//@${key}@/${val}}"
  done
  cp -a "${ASOUND}" "${ASOUND}.bak.$(date +%Y%m%d%H%M%S)" 2>/dev/null || true
  printf '%s\n' "${content}" > "${ASOUND}"
}

cmd_status() {
  if [[ -f "${STATE_FILE}" ]]; then
    cat "${STATE_FILE}"
  else
    echo "mode=unknown (no ${STATE_FILE})"
  fi
  echo "---"
  echo "${ASOUND}:"
  cat "${ASOUND}" 2>/dev/null || echo "(missing)"
}

cmd_aux() {
  local card
  card="$(detect_aux_card)"
  install_asound_from_template "${AUDIO_DIR}/asound.aux.conf" "CARD=${card}"
  write_state "aux" "${card}"
  apply_speech_dispatcher_alsa
  echo "Audio output: aux jack (ALSA hw:${card},0)"
}

cmd_i2s() {
  local card="${Braillatron_AUX_CARD:-0}"
  cp -a "${ASOUND}" "${ASOUND}.bak.$(date +%Y%m%d%H%M%S)" 2>/dev/null || true
  install -m 644 "${AUDIO_DIR}/asound.i2s.conf" "${ASOUND}"
  write_state "i2s" "${card}"
  apply_speech_dispatcher_alsa
  echo "Audio output: I2S amp (ALSA hw:1,0). Requires rk3566-i2s1-overlay + reboot."
}

cmd_bluetooth() {
  if [[ ! -f "${BT_CONF}" ]]; then
    echo "Missing ${BT_CONF}. Run setup-bluetooth-audio.sh first." >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  source "${BT_CONF}"
  if [[ -z "${device_mac:-}" ]]; then
    echo "device_mac not set in ${BT_CONF}" >&2
    exit 1
  fi

  systemctl enable --now bluealsa 2>/dev/null || true
  systemctl enable --now bluetooth 2>/dev/null || true

  install_asound_from_template "${AUDIO_DIR}/asound.bluetooth.conf" "MAC=${device_mac}"
  local card
  card="$(detect_aux_card)"
  write_state "bluetooth" "${card}"
  apply_speech_dispatcher_alsa
  echo "Audio output: Bluetooth ${device_mac} (BlueALSA A2DP)"
  echo "Ensure the speaker is connected: bluetoothctl connect ${device_mac}"
}

main() {
  require_root

  local mode=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --no-restart-ui)
        NO_RESTART_UI=1
        shift
        ;;
      aux|bluetooth|bt|i2s|status)
        if [[ -n "${mode}" ]]; then
          usage
        fi
        mode="$1"
        shift
        ;;
      *)
        usage
        ;;
    esac
  done

  [[ -n "${mode}" ]] || usage

  case "${mode}" in
    aux) cmd_aux ;;
    bluetooth|bt) cmd_bluetooth ;;
    i2s) cmd_i2s ;;
    status) cmd_status ;;
    *) usage ;;
  esac

  if [[ "${NO_RESTART_UI}" -eq 0 ]]; then
    systemctl restart braillatron-ui 2>/dev/null || true
  fi
}

main "$@"
